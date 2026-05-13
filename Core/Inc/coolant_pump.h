/**
  ******************************************************************************
  * @file    coolant_pump.h
  * @brief   Automatic coolant pump control
  *
  * Listens to two CAN inputs and decides whether the coolant pump must run:
  *  1. Inverter coolant temperature (INV_Temperatures_3 / INV_Coolant_Temp)
  *     above COOLANT_PUMP_TEMP_ON_C — pump is forced ON regardless of RTD.
  *     Hysteresis releases the override when temperature drops below
  *     (COOLANT_PUMP_TEMP_ON_C - COOLANT_PUMP_TEMP_HYST_C).
  *  2. VCU RTD switch (VCU_Summary / VCU_RTD_Active) — directly drives the
  *     pump while the temperature override is inactive.
  *
  * The effective request is OR'd with whatever the VCU/RPI command mask
  * currently holds for the pump bit, inside power_manager.
  ******************************************************************************
  */

#ifndef __COOLANT_PUMP_H
#define __COOLANT_PUMP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "can_manager.h"
#include <stdint.h>
#include <stdbool.h>

/* Hard-coded thresholds (degrees C) ----------------------------------------*/
#define COOLANT_PUMP_TEMP_ON_C       30
#define COOLANT_PUMP_TEMP_HYST_C     2

/** Initialize internal state and mutex. Must be called before tasks run. */
HAL_StatusTypeDef CoolantPump_Init(void);

/** True if the coolant pump should run based on auto inputs. */
bool CoolantPump_GetDesiredPump(void);

/* CAN dispatch entries -----------------------------------------------------*/
bool CoolantPump_MatchInverterTemps(const CAN_Message_t *msg);
void CoolantPump_HandleInverterTemps(const CAN_Message_t *msg);
bool CoolantPump_MatchVcuSummary(const CAN_Message_t *msg);
void CoolantPump_HandleVcuSummary(const CAN_Message_t *msg);

#ifdef __cplusplus
}
#endif

#endif /* __COOLANT_PUMP_H */
