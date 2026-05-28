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
    [POWER_RADIATOR_FANS] = { RAD_Ctrl_GPIO_Port,  RAD_Ctrl_Pin  },
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

    uint8_t        temp_manual_override_mask;
    uint32_t       temp_manual_override_tick;
    uint8_t        temp_manual_override_active;

    /* Acc Fans alternation state. While active, DRS and Fans are driven by
     * acc_fans_phase rather than by the VCU mask bits, and they flip every
     * ACC_FANS_TOGGLE_PERIOD_MS. The effective `acc_fans_active` is the OR
     * of the VCU command request and the HVC temperature override. */
    uint8_t        acc_fans_cmd_active;  /* from VCU_MOBO_Command bit 4 */
    uint8_t        acc_fans_temp_override; /* latched from HVC Acc_Temp_Max_C */
    uint8_t        acc_fans_active;
    uint8_t        acc_fans_phase;       /* 0 = DRS, 1 = Fans */
    uint32_t       acc_fans_last_toggle;

    uint8_t        radiator_fans_temp_request;
    uint8_t        inverter_motor_speed_valid;
    int16_t        inverter_motor_speed_rpm;
} g_pm;

static osMutexId_t g_mutex = NULL;
static const osMutexAttr_t g_mutex_attr = { .name = "PowerMutex" };

#define POWER_STARTUP_LOCKOUT_MASK \
    (POWER_MASK_PUMP | POWER_MASK_DRS | POWER_MASK_FANS | POWER_MASK_RADIATOR_FANS)

#define POWER_TEMP_MANUAL_OVERRIDE_MASK \
    (POWER_MASK_PUMP | POWER_MASK_RADIATOR_FANS)

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

/* Caller must hold g_mutex. Re-evaluate the Acc Fans active flag from its
 * input sources (VCU command request OR HVC temperature override). On a
 * fresh OFF -> ON transition, reset the alternation phase to DRS so the
 * first 30 s window is deterministic. */
static void Power_UpdateAccFansActive(uint32_t now)
{
    uint8_t new_active = (g_pm.acc_fans_cmd_active || g_pm.acc_fans_temp_override)
                         ? 1U : 0U;
    if (new_active && !g_pm.acc_fans_active) {
        g_pm.acc_fans_phase       = 0;
        g_pm.acc_fans_last_toggle = now;
    }
    g_pm.acc_fans_active = new_active;
}

/* Caller must hold g_mutex. Update the radiator-fan thermal latch from the
 * cached inverter coolant temperature. Speed inhibition is applied later when
 * building the effective output mask so the thermal latch can resume cleanly
 * once motor speed falls back below the limit. */
static void Power_UpdateRadiatorFansRequest(void)
{
    int16_t coolant_temp_dC = 0;
    const int16_t on_dC  = (int16_t)(RADIATOR_FANS_TEMP_ON_C  * 10);
    const int16_t off_dC = (int16_t)(RADIATOR_FANS_TEMP_OFF_C * 10);

    if (!CoolantPump_GetCoolantTempDeciC(&coolant_temp_dC)) {
        return;
    }

    if (coolant_temp_dC >= on_dC) {
        g_pm.radiator_fans_temp_request = 1U;
    } else if (coolant_temp_dC <= off_dC) {
        g_pm.radiator_fans_temp_request = 0U;
    }
}

/* Caller must hold g_mutex. Return true while the manual override window is
 * still active; once it expires, automatic control resumes. */
static bool Power_TempManualOverrideActive(uint32_t now)
{
    if (g_pm.temp_manual_override_active != 0U &&
        (now - g_pm.temp_manual_override_tick) >= POWER_TEMP_MANUAL_OVERRIDE_MS) {
        g_pm.temp_manual_override_active = 0U;
    }

    return g_pm.temp_manual_override_active != 0U;
}

static bool Power_StartupLockoutActive(uint32_t now)
{
    return now < (uint32_t)POWER_HIGH_CURRENT_STARTUP_LOCKOUT_MS;
}

/* Caller must hold g_mutex. Refresh the timed manual override window for the
 * temp-controlled outputs from the latest received command bits. */
static void Power_RefreshTempManualOverride(uint8_t mask, uint32_t now)
{
    g_pm.temp_manual_override_mask = mask & POWER_TEMP_MANUAL_OVERRIDE_MASK;
    g_pm.temp_manual_override_tick = now;
    g_pm.temp_manual_override_active = 1U;
}

/* Caller must hold g_mutex. Drive GPIOs from g_pm.commanded_mask plus any
 * internal overrides (auto coolant pump, Acc Fans alternation, automatic
 * radiator-fan control). DRS and Fans are unconditionally clamped to mutual
 * exclusion at the end. */
static void Power_RecomputeAndDriveGPIO(uint32_t now)
{
    uint8_t effective = g_pm.commanded_mask;
    bool radiator_fans_speed_allowed;
    bool temp_manual_override_active;
    bool startup_lockout_active;

    Power_UpdateRadiatorFansRequest();

    temp_manual_override_active = Power_TempManualOverrideActive(now);
    startup_lockout_active = Power_StartupLockoutActive(now);

    /* Pump and Radiator Fans are logic-controlled by default. Their command
     * bits are only honored during the timed manual override window. */
    effective &= (uint8_t)~POWER_TEMP_MANUAL_OVERRIDE_MASK;

    if (temp_manual_override_active) {
        effective |= g_pm.temp_manual_override_mask;
    } else {
        if (CoolantPump_GetDesiredPump()) {
            effective |= POWER_MASK_PUMP;
        }

        radiator_fans_speed_allowed = g_pm.inverter_motor_speed_valid &&
                                      (g_pm.inverter_motor_speed_rpm <= RADIATOR_FANS_MAX_MOTOR_SPEED_RPM);

        /* Radiator Fans are fully auto-controlled: apply the thermal latch when
         * speed is within range, otherwise force the output off. */
        if (g_pm.radiator_fans_temp_request && radiator_fans_speed_allowed) {
            effective |= POWER_MASK_RADIATOR_FANS;
        }
    }

    /* Acc Fans alternation: replace the VCU-supplied DRS/Fans bits with the
     * current alternation phase so exactly one of them is driven. */
    if (g_pm.acc_fans_active) {
        effective &= (uint8_t)~(POWER_MASK_DRS | POWER_MASK_FANS);
        effective |= (g_pm.acc_fans_phase == 0U) ? POWER_MASK_DRS
                                                  : POWER_MASK_FANS;
    }

    /* Hard mutual-exclusion backstop: DRS and Fans must NEVER be on at the
     * same time, regardless of source. If both are set, drop Fans and raise
     * a relay fault so the conflict is visible on the bus. */
    if ((effective & POWER_MASK_DRS) && (effective & POWER_MASK_FANS)) {
        effective &= (uint8_t)~POWER_MASK_FANS;
        ErrorMgr_SetError(ERROR_RELAY_FAULT);
    }

    if (startup_lockout_active) {
        effective &= (uint8_t)~POWER_STARTUP_LOCKOUT_MASK;
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

void PowerMgr_SetAccFansMode(bool enabled)
{
    if (osMutexAcquire(g_mutex, osWaitForever) != osOK) {
        return;
    }
    uint32_t now = osKernelGetTickCount();
    g_pm.acc_fans_cmd_active = enabled ? 1U : 0U;
    Power_UpdateAccFansActive(now);
    g_pm.last_command_tick = now;
    Power_RecomputeAndDriveGPIO(now);
    osMutexRelease(g_mutex);
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
        out->acc_fans_active   = g_pm.acc_fans_active;
        out->acc_fans_phase    = g_pm.acc_fans_phase;
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
    uint32_t now;

    if (msg->length < 2U) return;
    uint8_t enable = msg->data[0] & POWER_VCU_ENABLE_BIT;
    uint8_t mask   = msg->data[1] & POWER_MASK_ALL;
    /* Acc Fans request: byte 1 bit 4. */
    bool    acc_fans = ((msg->data[1] >> 4) & 0x01U) != 0U;
    if (!enable) {
        mask     = 0;
        acc_fans = false;
    }

    if (osMutexAcquire(g_mutex, osWaitForever) != osOK) return;

    now = osKernelGetTickCount();
    g_pm.acc_fans_cmd_active = acc_fans ? 1U : 0U;
    Power_UpdateAccFansActive(now);
    Power_RefreshTempManualOverride(mask, now);
    Power_ApplyMaskInternal(mask, now);

    g_pm.pending_persist      = 1;
    g_pm.pending_persist_mask = mask;
    g_pm.pending_persist_tick = now;

    osMutexRelease(g_mutex);
}

/* ------------------------------------------------------------------------ */
/* HVC ACC_Summary (extended 29-bit, ID 0x004001F5)                          */
/* ------------------------------------------------------------------------ */

bool PowerMgr_MatchHvcAccSummary(const CAN_Message_t *msg)
{
    return (msg != NULL) && (msg->id == HVC_ACC_SUMMARY_ID);
}

void PowerMgr_HandleHvcAccSummary(const CAN_Message_t *msg)
{
    if (msg == NULL || msg->length < 8U) return;
    /* Acc_Temp_Max_C : 48|16@1- (0.1 degC/LSB), bytes 6..7 little-endian. */
    int16_t temp_dC = (int16_t)((uint16_t)msg->data[6] |
                                ((uint16_t)msg->data[7] << 8));

    const int16_t on_dC  = (int16_t)(ACC_FANS_TEMP_ON_C  * 10);
    const int16_t off_dC = (int16_t)(ACC_FANS_TEMP_OFF_C * 10);

    if (osMutexAcquire(g_mutex, osWaitForever) != osOK) return;
    if (temp_dC >= on_dC) {
        g_pm.acc_fans_temp_override = 1;
    } else if (temp_dC <= off_dC) {
        g_pm.acc_fans_temp_override = 0;
    }
    /* Between on_dC and off_dC: hold previous override state. */
    uint32_t now = osKernelGetTickCount();
    Power_UpdateAccFansActive(now);
    Power_RecomputeAndDriveGPIO(now);
    osMutexRelease(g_mutex);
}

/* ------------------------------------------------------------------------ */
/* Inverter Motor_Position_Info (standard 11-bit, ID 0x0A5)                  */
/* ------------------------------------------------------------------------ */

bool PowerMgr_MatchInverterMotorPosition(const CAN_Message_t *msg)
{
    return (msg != NULL) && (msg->id == INV_MOTOR_POSITION_INFO_ID);
}

void PowerMgr_HandleInverterMotorPosition(const CAN_Message_t *msg)
{
    int16_t motor_speed_rpm;
    uint32_t now;

    if (msg == NULL || msg->length < 4U) return;

    /* INV_Motor_Speed : 16|16@1- (1 RPM/LSB), bytes 2..3 little-endian. */
    motor_speed_rpm = (int16_t)((uint16_t)msg->data[2] |
                                ((uint16_t)msg->data[3] << 8));

    if (osMutexAcquire(g_mutex, osWaitForever) != osOK) return;

    g_pm.inverter_motor_speed_rpm = motor_speed_rpm;
    g_pm.inverter_motor_speed_valid = 1U;
    now = osKernelGetTickCount();
    Power_RecomputeAndDriveGPIO(now);

    osMutexRelease(g_mutex);
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

            /* Acc Fans toggle: flip the alternation phase on schedule. */
            if (g_pm.acc_fans_active &&
                (now - g_pm.acc_fans_last_toggle) >= ACC_FANS_TOGGLE_PERIOD_MS) {
                g_pm.acc_fans_phase      ^= 1U;
                g_pm.acc_fans_last_toggle = now;
            }

            Power_RecomputeAndDriveGPIO(now);

            osMutexRelease(g_mutex);

            if (do_persist) {
                (void)Config_SaveDesiredRelayMask(mask_to_persist);
            }
        }

        osDelay(POWER_TASK_PERIOD_MS);
    }
}
