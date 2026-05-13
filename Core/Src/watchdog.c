/**
  ******************************************************************************
  * @file    watchdog.c
  ******************************************************************************
  */

#include "watchdog.h"
#include "error_manager.h"
#include "stm32l4xx_hal.h"

static IWDG_HandleTypeDef hiwdg;
static volatile uint32_t  g_last_hb[WD_TASK_COUNT] = {0};
static uint32_t           g_reset_cause = 0;
static uint8_t            g_was_iwdg_reset = 0;
static uint8_t            g_initialized = 0;

HAL_StatusTypeDef Watchdog_Init(void)
{
    /* Capture and clear the reset-cause flags from the previous reset so
     * subsequent boots can tell IWDG / soft-reset / brown-out apart. */
    g_reset_cause    = HAL_RCC_GetResetSource();
    g_was_iwdg_reset = ((g_reset_cause & RCC_RESET_FLAG_IWDG) != 0U) ? 1U : 0U;
    __HAL_RCC_CLEAR_RESET_FLAGS();

    if (g_was_iwdg_reset) {
        ErrorMgr_SetError(ERROR_WATCHDOG);
    }

    /* Freeze IWDG when the core is halted by the debugger. Harmless when
     * no debugger is attached. */
    SET_BIT(DBGMCU->APB1FZR1, DBGMCU_APB1FZR1_DBG_IWDG_STOP);

    hiwdg.Instance       = IWDG;
    hiwdg.Init.Prescaler = WATCHDOG_IWDG_PRESCALER;
    hiwdg.Init.Reload    = WATCHDOG_IWDG_RELOAD;
    hiwdg.Init.Window    = IWDG_WINDOW_DISABLE;
    if (HAL_IWDG_Init(&hiwdg) != HAL_OK) {
        return HAL_ERROR;
    }

    uint32_t now = osKernelGetTickCount();
    for (uint8_t i = 0; i < WD_TASK_COUNT; i++) {
        g_last_hb[i] = now;
    }
    g_initialized = 1;
    return HAL_OK;
}

void Watchdog_Heartbeat(WD_TaskId_t task)
{
    if ((uint32_t)task < (uint32_t)WD_TASK_COUNT) {
        g_last_hb[task] = osKernelGetTickCount();
    }
}

uint32_t Watchdog_GetResetCause(void)
{
    return g_reset_cause;
}

uint8_t Watchdog_WasIwdgReset(void)
{
    return g_was_iwdg_reset;
}

void WatchdogTask(void *argument)
{
    (void)argument;

    /* Re-seed all heartbeats once the scheduler is running so a slow-to-
     * start task is not flagged before its first iteration. */
    uint32_t now = osKernelGetTickCount();
    for (uint8_t i = 0; i < WD_TASK_COUNT; i++) {
        g_last_hb[i] = now;
    }

    const uint32_t warmup_deadline = now + WATCHDOG_WARMUP_MS;

    for (;;) {
        now = osKernelGetTickCount();

        bool all_alive = true;
        if ((int32_t)(now - warmup_deadline) >= 0 && g_initialized) {
            for (uint8_t i = 0; i < WD_TASK_COUNT; i++) {
                if ((now - g_last_hb[i]) > WATCHDOG_HEARTBEAT_DEADLINE_MS) {
                    all_alive = false;
                    break;
                }
            }
        }

        if (all_alive) {
            HAL_IWDG_Refresh(&hiwdg);
        } else {
            ErrorMgr_SetError(ERROR_WATCHDOG);
            /* Intentionally do NOT refresh — let IWDG reset the system. */
        }

        osDelay(WATCHDOG_TASK_PERIOD_MS);
    }
}
