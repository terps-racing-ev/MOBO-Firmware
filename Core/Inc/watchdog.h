/**
  ******************************************************************************
  * @file    watchdog.h
  * @brief   IWDG plus per-task heartbeats. Background task only kicks the
  *          IWDG when every registered task has heartbeated within its
  *          deadline.
  ******************************************************************************
  */

#ifndef __WATCHDOG_H
#define __WATCHDOG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "cmsis_os.h"
#include <stdint.h>

/** Task identifiers (compile-time bounded). */
typedef enum {
    WD_TASK_CAN     = 0,
    WD_TASK_POWER   = 1,
    WD_TASK_SENSOR  = 2,
    WD_TASK_SAFETY  = 3,
    WD_TASK_COUNT
} WD_TaskId_t;

#define WATCHDOG_TASK_PERIOD_MS         100U
#define WATCHDOG_HEARTBEAT_DEADLINE_MS  1500U
#define WATCHDOG_WARMUP_MS              1500U
/* IWDG: LSI ~32 kHz, prescaler /32, reload 2000 -> ~2 s timeout. Same
 * configuration as BMS-Firmware-RTOS, which is known to coexist with the
 * shared bootloader without spurious resets. */
#define WATCHDOG_IWDG_PRESCALER         IWDG_PRESCALER_32
#define WATCHDOG_IWDG_RELOAD            2000U

HAL_StatusTypeDef Watchdog_Init(void);
void              WatchdogTask(void *argument);
void              Watchdog_Heartbeat(WD_TaskId_t task);
uint32_t          Watchdog_GetResetCause(void);
uint8_t           Watchdog_WasIwdgReset(void);

#ifdef __cplusplus
}
#endif

#endif /* __WATCHDOG_H */
