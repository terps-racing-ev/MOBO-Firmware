/**
  ******************************************************************************
  * @file    power_manager.h
  * @brief   High-power output (relay) management
  *
    * Sole owner of the four relay GPIOs (Pump, DRS, Fans, Radiator Fans). Accepts
  * relay commands from the VCU only. Persists the last commanded relay mask
  * to flash via config_manager so vehicle state is restored after a power
  * cycle.
  *
  * Mask bit layout (used by both the persisted state and the CAN command
  * payload byte 1):
  *   bit 0 : Pump
  *   bit 1 : DRS
  *   bit 2 : Fans
    *   bit 3 : Radiator Fans
  *
  * Acc Fans mode
  * -------------
  * In addition to the relay mask, the VCU may set the Acc Fans bit (byte 1
  * bit 4). While Acc Fans is active, the firmware IGNORES the VCU-supplied
  * DRS and Fans bits and instead alternates DRS and Fans at
  * ACC_FANS_TOGGLE_PERIOD_MS so exactly one of them is on at any time.
   *
   * Radiator Fans auto control
   * --------------------------
  * The Pump and Radiator Fans outputs are logic-controlled by default. A
  * received MOBO power command can manually force either channel ON or OFF
  * for up to POWER_TEMP_MANUAL_OVERRIDE_MS, after which automatic control
  * resumes. Radiator Fans otherwise follow inverter coolant temperature and
  * inverter motor speed, latching ON at RADIATOR_FANS_TEMP_ON_C, releasing
  * at RADIATOR_FANS_TEMP_OFF_C, and forcing OFF whenever motor speed exceeds
  * RADIATOR_FANS_MAX_MOTOR_SPEED_RPM.
  *
  * Mutual exclusion between DRS and Fans is enforced unconditionally: the
  * effective mask is post-clamped so DRS and Fans can NEVER be on together,
  * regardless of the source (VCU mask, Acc Fans alternation, internal
  * overrides). A conflict raises ERROR_RELAY_FAULT.
  ******************************************************************************
  */

#ifndef __POWER_MANAGER_H
#define __POWER_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "cmsis_os.h"
#include "can_manager.h"
#include <stdint.h>
#include <stdbool.h>

/* Channel enumeration ------------------------------------------------------*/
typedef enum {
    POWER_PUMP           = 0,
    POWER_DRS            = 1,
    POWER_FANS           = 2,
    POWER_RADIATOR_FANS  = 3,
    POWER_CHANNEL_COUNT,
} Power_Channel_t;

#define POWER_MASK_PUMP            (1U << POWER_PUMP)
#define POWER_MASK_DRS             (1U << POWER_DRS)
#define POWER_MASK_FANS            (1U << POWER_FANS)
#define POWER_MASK_RADIATOR_FANS   (1U << POWER_RADIATOR_FANS)
#define POWER_MASK_ALL             0x0FU

/* Acc Fans alternation period: while Acc Fans is active, DRS and Fans swap
 * every ACC_FANS_TOGGLE_PERIOD_MS milliseconds. */
#define ACC_FANS_TOGGLE_PERIOD_MS  30000U

/* Acc Fans temperature override (sourced from HVC ACC_Summary /
 * Acc_Temp_Max_C). The override latches ON when the max accumulator cell
 * temperature reaches ACC_FANS_TEMP_ON_C and releases when it falls back to
 * ACC_FANS_TEMP_OFF_C. While the override is active, Acc Fans alternation
 * engages even if the VCU has not asserted Acc_Fans_Request. */
#define ACC_FANS_TEMP_ON_C         45
#define ACC_FANS_TEMP_OFF_C        40

/* Pump auto-control thresholds (sourced from inverter coolant temperature via
 * INV_Temperatures_3 / INV_Coolant_Temp). The pump temperature override
 * latches ON at COOLANT_PUMP_TEMP_ON_C and releases when temperature drops by
 * COOLANT_PUMP_TEMP_HYST_C. */
#define COOLANT_PUMP_TEMP_ON_C            37
#define COOLANT_PUMP_TEMP_HYST_C          2

/* Startup lockout: all high-current loads are held OFF for at least this many
 * milliseconds after boot. This includes the pump, both Acc Fans outputs, and
 * the radiator fans, preventing their inrush current from coinciding with the
 * rest of the system powering up. */
#define POWER_HIGH_CURRENT_STARTUP_LOCKOUT_MS   10000U

/* Radiator Fans auto-control thresholds (sourced from inverter coolant temp
 * and INV_Motor_Speed). The thermal request latches ON at
 * RADIATOR_FANS_TEMP_ON_C and releases at RADIATOR_FANS_TEMP_OFF_C.
 * Regardless of temperature, the output is inhibited above
 * RADIATOR_FANS_MAX_MOTOR_SPEED_RPM. */
#define RADIATOR_FANS_TEMP_ON_C            42
#define RADIATOR_FANS_TEMP_OFF_C           40
#define RADIATOR_FANS_MAX_MOTOR_SPEED_RPM  2380

/* A received MOBO power command manually overrides the automatic pump and
 * radiator-fan logic for up to this long. Once the window expires, both
 * channels return to automatic control. */
#define POWER_TEMP_MANUAL_OVERRIDE_MS      60000U

/* Per-channel FSM ----------------------------------------------------------*/
typedef enum {
    RELAY_OFF          = 0,
    RELAY_TURNING_ON   = 1,
    RELAY_ON           = 2,
    RELAY_TURNING_OFF  = 3,
    RELAY_FAULT        = 4,
} Relay_State_t;

/* Snapshot for telemetry ---------------------------------------------------*/
typedef struct {
    uint8_t        commanded_mask;     /**< Last accepted mask */
    uint8_t        actual_mask;        /**< GPIO readback */
    Relay_State_t  state[POWER_CHANNEL_COUNT];
    uint32_t       ms_since_last_command;
    uint8_t        acc_fans_active;    /**< 1 while Acc Fans alternation is engaged */
    uint8_t        acc_fans_phase;     /**< 0 = DRS driven, 1 = Fans driven */
} Power_Snapshot_t;

/* Public API ---------------------------------------------------------------*/

/** Initialize FSM, restore desired mask from flash (does NOT apply yet). */
HAL_StatusTypeDef PowerMgr_Init(void);

/** Main task body. */
void PowerMgrTask(void *argument);

/**
 * @brief  Apply a relay mask.
 * @param  mask: bit i = 1 -> channel i ON, 0 -> OFF
 * @retval true on accept.
 */
bool PowerMgr_ApplyMask(uint8_t mask);

/**
 * @brief  Enable or disable Acc Fans alternation mode.
 *
 * While active, the firmware ignores the VCU-supplied DRS and Fans bits and
 * alternates DRS and Fans every ACC_FANS_TOGGLE_PERIOD_MS so exactly one of
 * them is on at a time. Mutual exclusion is enforced unconditionally.
 */
void PowerMgr_SetAccFansMode(bool enabled);

/** Snapshot of current state for telemetry. */
void PowerMgr_GetSnapshot(Power_Snapshot_t *out);

/* CAN dispatch entries ----------------------------------------------------*/
bool PowerMgr_MatchVcuCommand(const CAN_Message_t *msg);
void PowerMgr_HandleVcuCommand(const CAN_Message_t *msg);
bool PowerMgr_MatchHvcAccSummary(const CAN_Message_t *msg);
void PowerMgr_HandleHvcAccSummary(const CAN_Message_t *msg);
bool PowerMgr_MatchInverterMotorPosition(const CAN_Message_t *msg);
void PowerMgr_HandleInverterMotorPosition(const CAN_Message_t *msg);

#ifdef __cplusplus
}
#endif

#endif /* __POWER_MANAGER_H */
