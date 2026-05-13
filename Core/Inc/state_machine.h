/**
  ******************************************************************************
  * @file    state_machine.h
  * @brief   MOBO system state machine
  *
  * Coarse-grained system state. MOBO is a dumb relay/sensor module — there
  * is no FAULT latch and no automatic safe-state. State exists only for boot
  * sequencing and telemetry.
  ******************************************************************************
  */

#ifndef __STATE_MACHINE_H
#define __STATE_MACHINE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "cmsis_os.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief MOBO system states.
 */
typedef enum {
    MOBO_STATE_INIT     = 0,  /**< Boot / peripheral init in progress */
    MOBO_STATE_STANDBY  = 1,  /**< Init complete; promoted to ACTIVE shortly */
    MOBO_STATE_ACTIVE   = 2,  /**< Normal operation */
} MOBO_State_t;

/** Initialize the state machine. Starts in MOBO_STATE_INIT. */
HAL_StatusTypeDef StateMachine_Init(void);

/** Get current state (thread-safe snapshot). */
MOBO_State_t StateMachine_GetState(void);

/** Unconditionally set state. */
void StateMachine_SetState(MOBO_State_t new_state);

#ifdef __cplusplus
}
#endif

#endif /* __STATE_MACHINE_H */
