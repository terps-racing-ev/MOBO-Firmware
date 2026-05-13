/**
  ******************************************************************************
  * @file    error_manager.h
  * @brief   MOBO error/warning bitmask manager
  *
  * Two 32-bit bitmasks (errors + warnings) partitioned into 4 categories of
  * 8 bits each. Pure telemetry — MOBO never self-disables on errors.
  ******************************************************************************
  */

#ifndef __ERROR_MANAGER_H
#define __ERROR_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "cmsis_os.h"
#include "state_machine.h"
#include <stdint.h>
#include <stdbool.h>

/* ---------------------------------------------------------------------------
 * Error flag bit definitions (Byte 0..3 of error_flags)
 * -------------------------------------------------------------------------*/

/* Byte 0: Power / Relay ----------------------------------------------------*/
#define ERROR_RELAY_FAULT          (1U << 0)  /**< Relay actual != commanded after settle */
#define ERROR_OVER_VOLTAGE         (1U << 1)  /**< Battery rail above limit */
#define ERROR_UNDER_VOLTAGE_LV     (1U << 2)  /**< Battery rail below limit */
#define ERROR_5V_RAIL_FAULT        (1U << 3)  /**< 5V sense out of tolerance */
#define ERROR_POWER_RESERVED_4     (1U << 4)
#define ERROR_POWER_RESERVED_5     (1U << 5)
#define ERROR_POWER_RESERVED_6     (1U << 6)
#define ERROR_POWER_RESERVED_7     (1U << 7)

/* Byte 1: Current / Sensor -------------------------------------------------*/
#define ERROR_LV_OVERCURRENT       (1U << 8)
#define ERROR_HC_OVERCURRENT       (1U << 9)
#define ERROR_ADC_FAULT            (1U << 10) /**< ADC conversion failed or saturated */
#define ERROR_BRAKE_SENSOR_FAULT   (1U << 11)
#define ERROR_SENSOR_RESERVED_4    (1U << 12)
#define ERROR_SENSOR_RESERVED_5    (1U << 13)
#define ERROR_SENSOR_RESERVED_6    (1U << 14)
#define ERROR_SENSOR_RESERVED_7    (1U << 15)

/* Byte 2: Safety inputs (debounced) ----------------------------------------*/
#define ERROR_SDC1_OPEN            (1U << 16)
#define ERROR_SDC2_OPEN            (1U << 17)
#define ERROR_SDC3_OPEN            (1U << 18)
#define ERROR_BMS_FAULT            (1U << 19)
#define ERROR_BSPD_FAULT           (1U << 20)
#define ERROR_IMD_FAULT            (1U << 21)
#define ERROR_SAFETY_RESERVED_6    (1U << 22)
#define ERROR_SAFETY_RESERVED_7    (1U << 23)

/* Byte 3: Comm / System ----------------------------------------------------*/
#define ERROR_CAN_BUS_OFF          (1U << 24)
#define ERROR_CAN_TX_TIMEOUT       (1U << 25)
#define ERROR_CAN_RX_OVERFLOW      (1U << 26)
#define ERROR_WATCHDOG             (1U << 27)
#define ERROR_FLASH_FAULT          (1U << 28)
#define ERROR_COMMAND_TIMEOUT      (1U << 29)
#define ERROR_SYSTEM_RESERVED_6    (1U << 30)
#define ERROR_SYSTEM_RESERVED_7    (1U << 31)

/* ---------------------------------------------------------------------------
 * Warning flags (independent 32-bit space)
 * -------------------------------------------------------------------------*/
#define WARNING_LOW_BATTERY        (1U << 0)
#define WARNING_HIGH_LV_CURRENT    (1U << 8)
#define WARNING_HIGH_HC_CURRENT    (1U << 9)
#define WARNING_CAN_TX_QUEUE_FULL  (1U << 24)

/* Snapshot for telemetry frames --------------------------------------------*/
typedef struct {
    uint32_t error_flags;
    uint32_t warning_flags;
    uint16_t fault_count;     /**< Total unique error-set events since boot */
    uint32_t uptime_seconds;
} Error_Status_t;

/* Public API ---------------------------------------------------------------*/
HAL_StatusTypeDef ErrorMgr_Init(void);

/** Set one or more error bits (OR-mask). Pure telemetry. */
void ErrorMgr_SetError(uint32_t flags);

/** Clear one or more error bits. */
void ErrorMgr_ClearError(uint32_t flags);

void ErrorMgr_SetWarning(uint32_t flags);
void ErrorMgr_ClearWarning(uint32_t flags);

uint32_t ErrorMgr_GetErrors(void);
uint32_t ErrorMgr_GetWarnings(void);
bool     ErrorMgr_HasErrors(void);

void ErrorMgr_GetStatus(Error_Status_t *out);

/** Call once per second from CAN manager task. */
void ErrorMgr_TickUptime(void);

#ifdef __cplusplus
}
#endif

#endif /* __ERROR_MANAGER_H */
