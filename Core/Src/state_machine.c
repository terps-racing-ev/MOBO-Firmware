/**
  ******************************************************************************
  * @file    state_machine.c
  * @brief   MOBO system state machine implementation
  ******************************************************************************
  */

#include "state_machine.h"

static MOBO_State_t g_state = MOBO_STATE_INIT;
static osMutexId_t  g_state_mutex = NULL;

static const osMutexAttr_t g_state_mutex_attr = {
    .name = "StateMutex",
};

HAL_StatusTypeDef StateMachine_Init(void)
{
    g_state = MOBO_STATE_INIT;
    g_state_mutex = osMutexNew(&g_state_mutex_attr);
    if (g_state_mutex == NULL) {
        return HAL_ERROR;
    }
    return HAL_OK;
}

MOBO_State_t StateMachine_GetState(void)
{
    MOBO_State_t s = MOBO_STATE_INIT;
    if (osMutexAcquire(g_state_mutex, osWaitForever) == osOK) {
        s = g_state;
        osMutexRelease(g_state_mutex);
    }
    return s;
}

void StateMachine_SetState(MOBO_State_t new_state)
{
    if (osMutexAcquire(g_state_mutex, osWaitForever) == osOK) {
        g_state = new_state;
        osMutexRelease(g_state_mutex);
    }
}
