/**
  ******************************************************************************
  * @file    safety_monitor.h
  * @brief   Polls SDC/BMS/BSPD/IMD inputs with debounce, sets error flags
  *
  * Inputs are pulled up; a logic 0 on any of these lines indicates an
  * asserted/faulted safety circuit. A fault is declared after
  * SAFETY_DEBOUNCE_SAMPLES consecutive reads of the asserted state.
  ******************************************************************************
  */

#ifndef __SAFETY_MONITOR_H
#define __SAFETY_MONITOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "cmsis_os.h"
#include <stdint.h>
#include <stdbool.h>

#define SAFETY_TASK_PERIOD_MS    10U
#define SAFETY_DEBOUNCE_SAMPLES  3U

/* Bit positions used in MOBO_Safety_Status frame and debounced bitmask. */
#define SAFETY_BIT_SDC1   0
#define SAFETY_BIT_SDC2   1
#define SAFETY_BIT_SDC3   2
#define SAFETY_BIT_BMS    3
#define SAFETY_BIT_BSPD   4
#define SAFETY_BIT_IMD    5

typedef struct {
    uint8_t raw_mask;        /**< Latest sample (1 = fault) */
    uint8_t debounced_mask;  /**< After debounce filter */
    uint8_t latched_mask;    /**< OR of debounced over time; cleared on demand */
} Safety_Snapshot_t;

HAL_StatusTypeDef SafetyMonitor_Init(void);
void              SafetyMonitorTask(void *argument);
void              SafetyMonitor_GetSnapshot(Safety_Snapshot_t *out);
void              SafetyMonitor_ClearLatched(void);

#ifdef __cplusplus
}
#endif

#endif /* __SAFETY_MONITOR_H */
