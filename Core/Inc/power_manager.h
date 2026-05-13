/**
  ******************************************************************************
  * @file    power_manager.h
  * @brief   High-power output (relay) management
  *
  * Sole owner of the four relay GPIOs (Pump, DRS, Fans, Radiator). Accepts
  * relay commands from the VCU only. Persists the last commanded relay mask
  * to flash via config_manager so vehicle state is restored after a power
  * cycle.
  *
  * Mask bit layout (used by both the persisted state and the CAN command
  * payload byte 1):
  *   bit 0 : Pump
  *   bit 1 : DRS
  *   bit 2 : Fans
  *   bit 3 : Radiator
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
    POWER_PUMP = 0,
    POWER_DRS  = 1,
    POWER_FANS = 2,
    POWER_RAD  = 3,
    POWER_CHANNEL_COUNT,
} Power_Channel_t;

#define POWER_MASK_PUMP   (1U << POWER_PUMP)
#define POWER_MASK_DRS    (1U << POWER_DRS)
#define POWER_MASK_FANS   (1U << POWER_FANS)
#define POWER_MASK_RAD    (1U << POWER_RAD)
#define POWER_MASK_ALL    0x0FU

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

/** Snapshot of current state for telemetry. */
void PowerMgr_GetSnapshot(Power_Snapshot_t *out);

/* CAN dispatch entries ----------------------------------------------------*/
bool PowerMgr_MatchVcuCommand(const CAN_Message_t *msg);
void PowerMgr_HandleVcuCommand(const CAN_Message_t *msg);

#ifdef __cplusplus
}
#endif

#endif /* __POWER_MANAGER_H */
