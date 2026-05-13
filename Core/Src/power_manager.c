/**
  ******************************************************************************
  * @file    power_manager.c
  ******************************************************************************
  */

#include "power_manager.h"
#include "config_manager.h"
#include "state_machine.h"
#include "error_manager.h"
#include "coolant_pump.h"
#include "can_ids.h"
#include "watchdog.h"
#include <string.h>

#define POWER_TASK_PERIOD_MS         20U
#define POWER_RELAY_SETTLE_MS        50U
#define POWER_FLASH_DEBOUNCE_MS     500U
#define POWER_VCU_ENABLE_BIT        0x01U  /**< bit0 of VCU command byte 0 */

typedef struct {
    GPIO_TypeDef *port;
    uint16_t      pin;
} Relay_Pin_t;

static const Relay_Pin_t g_relay_pins[POWER_CHANNEL_COUNT] = {
    [POWER_PUMP] = { PUMP_Ctrl_GPIO_Port, PUMP_Ctrl_Pin },
    [POWER_DRS]  = { DRS_Ctrl_GPIO_Port,  DRS_Ctrl_Pin  },
    [POWER_FANS] = { FANS_Ctrl_GPIO_Port, FANS_Ctrl_Pin },
    [POWER_RAD]  = { RAD_Ctrl_GPIO_Port,  RAD_Ctrl_Pin  },
};

static struct {
    uint8_t        commanded_mask;
    uint8_t        applied_mask;
    Relay_State_t  state[POWER_CHANNEL_COUNT];
    uint32_t       state_entry_tick[POWER_CHANNEL_COUNT];

    uint32_t       last_command_tick;

    uint8_t        pending_persist_mask;
    uint32_t       pending_persist_tick;
    uint8_t        pending_persist;
    uint8_t        boot_restore_done;
} g_pm;

static osMutexId_t g_mutex = NULL;
static const osMutexAttr_t g_mutex_attr = { .name = "PowerMutex" };

/* ------------------------------------------------------------------------ */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------ */

static void Power_DriveGPIO(Power_Channel_t ch, bool on)
{
    HAL_GPIO_WritePin(g_relay_pins[ch].port, g_relay_pins[ch].pin,
                      on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static bool Power_ReadGPIO(Power_Channel_t ch)
{
    return HAL_GPIO_ReadPin(g_relay_pins[ch].port, g_relay_pins[ch].pin) == GPIO_PIN_SET;
}

static uint8_t Power_ReadActualMask(void)
{
    uint8_t m = 0;
    for (uint8_t i = 0; i < POWER_CHANNEL_COUNT; i++) {
        if (Power_ReadGPIO((Power_Channel_t)i)) {
            m |= (uint8_t)(1U << i);
        }
    }
    return m;
}

/* Caller must hold g_mutex. */
static void Power_RequestChannel(Power_Channel_t ch, bool desired_on, uint32_t now)
{
    bool currently_on = Power_ReadGPIO(ch);
    if (desired_on && !currently_on) {
        Power_DriveGPIO(ch, true);
        g_pm.state[ch] = RELAY_TURNING_ON;
        g_pm.state_entry_tick[ch] = now;
    } else if (!desired_on && currently_on) {
        Power_DriveGPIO(ch, false);
        g_pm.state[ch] = RELAY_TURNING_OFF;
        g_pm.state_entry_tick[ch] = now;
    }
}

/* Caller must hold g_mutex. Drive GPIOs from g_pm.commanded_mask plus any
 * internal overrides (currently just the auto coolant pump). */
static void Power_RecomputeAndDriveGPIO(uint32_t now)
{
    uint8_t effective = g_pm.commanded_mask;
    if (CoolantPump_GetDesiredPump()) {
        effective |= POWER_MASK_PUMP;
    }
    for (uint8_t i = 0; i < POWER_CHANNEL_COUNT; i++) {
        bool desired = (effective & (1U << i)) != 0U;
        Power_RequestChannel((Power_Channel_t)i, desired, now);
    }
}

/* Caller must hold g_mutex. */
static void Power_ApplyMaskInternal(uint8_t mask, uint32_t now)
{
    g_pm.commanded_mask    = mask;
    g_pm.last_command_tick = now;
    Power_RecomputeAndDriveGPIO(now);
}

static void Power_TickFSM(uint32_t now)
{
    for (uint8_t i = 0; i < POWER_CHANNEL_COUNT; i++) {
        bool actual = Power_ReadGPIO((Power_Channel_t)i);
        Relay_State_t s = g_pm.state[i];

        switch (s) {
        case RELAY_TURNING_ON:
            if ((now - g_pm.state_entry_tick[i]) >= POWER_RELAY_SETTLE_MS) {
                if (actual) {
                    g_pm.state[i] = RELAY_ON;
                } else {
                    g_pm.state[i] = RELAY_FAULT;
                    ErrorMgr_SetError(ERROR_RELAY_FAULT);
                }
            }
            break;
        case RELAY_TURNING_OFF:
            if ((now - g_pm.state_entry_tick[i]) >= POWER_RELAY_SETTLE_MS) {
                if (!actual) {
                    g_pm.state[i] = RELAY_OFF;
                } else {
                    g_pm.state[i] = RELAY_FAULT;
                    ErrorMgr_SetError(ERROR_RELAY_FAULT);
                }
            }
            break;
        case RELAY_ON:
        case RELAY_OFF:
        case RELAY_FAULT:
        default:
            break;
        }
    }
    g_pm.applied_mask = Power_ReadActualMask();
}

/* ------------------------------------------------------------------------ */
/* Public API                                                                */
/* ------------------------------------------------------------------------ */

HAL_StatusTypeDef PowerMgr_Init(void)
{
    g_mutex = osMutexNew(&g_mutex_attr);
    if (g_mutex == NULL) {
        return HAL_ERROR;
    }

    memset(&g_pm, 0, sizeof(g_pm));
    for (uint8_t i = 0; i < POWER_CHANNEL_COUNT; i++) {
        Power_DriveGPIO((Power_Channel_t)i, false);
        g_pm.state[i] = RELAY_OFF;
    }
    g_pm.last_command_tick = osKernelGetTickCount();

    return HAL_OK;
}

bool PowerMgr_ApplyMask(uint8_t mask)
{
    mask &= POWER_MASK_ALL;

    if (osMutexAcquire(g_mutex, osWaitForever) != osOK) {
        return false;
    }

    uint32_t now = osKernelGetTickCount();
    Power_ApplyMaskInternal(mask, now);

    /* Schedule a debounced flash persist. */
    g_pm.pending_persist      = 1;
    g_pm.pending_persist_mask = mask;
    g_pm.pending_persist_tick = now;

    osMutexRelease(g_mutex);
    return true;
}

void PowerMgr_GetSnapshot(Power_Snapshot_t *out)
{
    if (out == NULL) return;
    if (osMutexAcquire(g_mutex, osWaitForever) == osOK) {
        out->commanded_mask    = g_pm.commanded_mask;
        out->actual_mask       = g_pm.applied_mask;
        memcpy(out->state, g_pm.state, sizeof(out->state));
        uint32_t now = osKernelGetTickCount();
        out->ms_since_last_command = now - g_pm.last_command_tick;
        osMutexRelease(g_mutex);
    } else {
        memset(out, 0, sizeof(*out));
    }
}

/* ------------------------------------------------------------------------ */
/* CAN dispatch                                                              */
/* ------------------------------------------------------------------------ */

bool PowerMgr_MatchVcuCommand(const CAN_Message_t *msg)
{
    return (msg != NULL) && (msg->id == MOBO_VCU_POWER_CMD_ID);
}

void PowerMgr_HandleVcuCommand(const CAN_Message_t *msg)
{
    if (msg->length < 2U) return;
    uint8_t enable = msg->data[0] & POWER_VCU_ENABLE_BIT;
    uint8_t mask   = msg->data[1] & POWER_MASK_ALL;
    if (!enable) mask = 0;
    (void)PowerMgr_ApplyMask(mask);
}

/* ------------------------------------------------------------------------ */
/* Task                                                                      */
/* ------------------------------------------------------------------------ */

void PowerMgrTask(void *argument)
{
    (void)argument;
    osDelay(50);

    uint8_t restore_mask = Config_GetDesiredRelayMask() & POWER_MASK_ALL;

    for (;;) {
        Watchdog_Heartbeat(WD_TASK_POWER);

        uint32_t now = osKernelGetTickCount();

        if (osMutexAcquire(g_mutex, osWaitForever) == osOK) {
            /* Boot-restore: apply the saved mask once after the state machine
             * promotes to ACTIVE. */
            if (!g_pm.boot_restore_done &&
                StateMachine_GetState() == MOBO_STATE_ACTIVE) {
                if (restore_mask != 0U) {
                    Power_ApplyMaskInternal(restore_mask, now);
                }
                g_pm.boot_restore_done = 1;
            }

            uint8_t do_persist = 0;
            uint8_t mask_to_persist = 0;
            if (g_pm.pending_persist &&
                (now - g_pm.pending_persist_tick) >= POWER_FLASH_DEBOUNCE_MS) {
                do_persist = 1;
                mask_to_persist = g_pm.pending_persist_mask;
                g_pm.pending_persist = 0;
            }

            Power_TickFSM(now);
            Power_RecomputeAndDriveGPIO(now);

            osMutexRelease(g_mutex);

            if (do_persist) {
                (void)Config_SaveDesiredRelayMask(mask_to_persist);
            }
        }

        osDelay(POWER_TASK_PERIOD_MS);
    }
}
