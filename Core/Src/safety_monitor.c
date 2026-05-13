/**
  ******************************************************************************
  * @file    safety_monitor.c
  ******************************************************************************
  */

#include "safety_monitor.h"
#include "error_manager.h"
#include "state_machine.h"
#include "watchdog.h"
#include <string.h>

/* Polarity convention (matches existing AGENTS.md): GPIO read of LOW (PIN_RESET)
 * means the safety circuit is asserted/faulted (pull-up returns to ground when
 * the loop is open or the sense rail goes low). */

typedef struct {
    GPIO_TypeDef *port;
    uint16_t      pin;
    uint8_t       bit;
    uint32_t      error_flag;
} Safety_Input_t;

static const Safety_Input_t g_inputs[] = {
    { SDC_1_GPIO_Port, SDC_1_Pin, SAFETY_BIT_SDC1, ERROR_SDC1_OPEN },
    { SDC_2_GPIO_Port, SDC_2_Pin, SAFETY_BIT_SDC2, ERROR_SDC2_OPEN },
    { SDC_3_GPIO_Port, SDC_3_Pin, SAFETY_BIT_SDC3, ERROR_SDC3_OPEN },
    { BMS_GPIO_Port,   BMS_Pin,   SAFETY_BIT_BMS,  ERROR_BMS_FAULT  },
    { BSPD_GPIO_Port,  BSPD_Pin,  SAFETY_BIT_BSPD, ERROR_BSPD_FAULT },
    { IMD_GPIO_Port,   IMD_Pin,   SAFETY_BIT_IMD,  ERROR_IMD_FAULT  },
};
#define SAFETY_INPUT_COUNT (sizeof(g_inputs) / sizeof(g_inputs[0]))

static Safety_Snapshot_t g_snapshot = {0};
static uint8_t           g_history[SAFETY_DEBOUNCE_SAMPLES];
static uint8_t           g_history_idx = 0;
static osMutexId_t       g_mutex = NULL;

static const osMutexAttr_t g_mutex_attr = { .name = "SafetyMutex" };

HAL_StatusTypeDef SafetyMonitor_Init(void)
{
    g_mutex = osMutexNew(&g_mutex_attr);
    if (g_mutex == NULL) return HAL_ERROR;
    memset(&g_snapshot, 0, sizeof(g_snapshot));
    memset(g_history, 0, sizeof(g_history));
    return HAL_OK;
}

void SafetyMonitor_GetSnapshot(Safety_Snapshot_t *out)
{
    if (out == NULL) return;
    if (osMutexAcquire(g_mutex, osWaitForever) == osOK) {
        *out = g_snapshot;
        osMutexRelease(g_mutex);
    }
}

void SafetyMonitor_ClearLatched(void)
{
    if (osMutexAcquire(g_mutex, osWaitForever) == osOK) {
        g_snapshot.latched_mask = 0;
        osMutexRelease(g_mutex);
    }
}

static uint8_t Safety_SampleRaw(void)
{
    uint8_t mask = 0;
    for (uint8_t i = 0; i < SAFETY_INPUT_COUNT; i++) {
        if (HAL_GPIO_ReadPin(g_inputs[i].port, g_inputs[i].pin) == GPIO_PIN_RESET) {
            mask |= (uint8_t)(1U << g_inputs[i].bit);
        }
    }
    return mask;
}

static uint8_t Safety_Debounce(uint8_t raw)
{
    g_history[g_history_idx] = raw;
    g_history_idx = (g_history_idx + 1U) % SAFETY_DEBOUNCE_SAMPLES;
    /* A bit is set in the debounced output only when set in every history slot. */
    uint8_t out = 0xFFU;
    for (uint8_t i = 0; i < SAFETY_DEBOUNCE_SAMPLES; i++) {
        out &= g_history[i];
    }
    return out;
}

void SafetyMonitorTask(void *argument)
{
    (void)argument;
    osDelay(10);

    /* Prime the history buffer with current readings to avoid spurious clean
     * indication on the first few samples. */
    for (uint8_t i = 0; i < SAFETY_DEBOUNCE_SAMPLES; i++) {
        g_history[i] = Safety_SampleRaw();
    }

    uint8_t prev_debounced = 0xFFU;
    bool first_clean_seen  = false;

    for (;;) {
        Watchdog_Heartbeat(WD_TASK_SAFETY);

        uint8_t raw       = Safety_SampleRaw();
        uint8_t debounced = Safety_Debounce(raw);

        if (osMutexAcquire(g_mutex, osWaitForever) == osOK) {
            g_snapshot.raw_mask        = raw;
            g_snapshot.debounced_mask  = debounced;
            g_snapshot.latched_mask   |= debounced;
            osMutexRelease(g_mutex);
        }

        /* Translate debounced bits into error flags (set on rising, clear on falling). */
        uint8_t newly_set     = debounced & ~prev_debounced;
        uint8_t newly_cleared = prev_debounced & ~debounced;
        for (uint8_t i = 0; i < SAFETY_INPUT_COUNT; i++) {
            uint8_t b = (uint8_t)(1U << g_inputs[i].bit);
            if ((newly_set & b) != 0U) {
                ErrorMgr_SetError(g_inputs[i].error_flag);
            }
            if ((newly_cleared & b) != 0U) {
                ErrorMgr_ClearError(g_inputs[i].error_flag);
            }
        }
        prev_debounced = debounced;

        /* Promote STANDBY -> ACTIVE unconditionally after first sample.
         * Safety inputs are pure telemetry on MOBO; they do not gate
         * command acceptance. */
        if (!first_clean_seen && StateMachine_GetState() == MOBO_STATE_STANDBY) {
            StateMachine_SetState(MOBO_STATE_ACTIVE);
            first_clean_seen = true;
        }

        osDelay(SAFETY_TASK_PERIOD_MS);
    }
}
