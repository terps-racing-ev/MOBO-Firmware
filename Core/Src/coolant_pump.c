/**
  ******************************************************************************
  * @file    coolant_pump.c
  ******************************************************************************
  */

#include "coolant_pump.h"
#include "can_ids.h"
#include "cmsis_os.h"
#include <string.h>

/* Internal state. Temperatures stored in deci-degC (0.1 C/LSB) to match the
 * inverter's wire format, avoiding float math. */
static struct {
    int16_t  coolant_temp_dC;   /**< latest INV_Coolant_Temp, 0.1 C/LSB */
    bool     temp_valid;        /**< true once first inverter frame received */
    bool     temp_override;     /**< latched ON above threshold, OFF below hyst */
    bool     rtd_active;        /**< latest VCU_RTD_Active */
} g_cp;

static osMutexId_t g_mutex = NULL;
static const osMutexAttr_t g_mutex_attr = { .name = "CoolantMutex" };

#define TEMP_ON_DC    ((int16_t)(COOLANT_PUMP_TEMP_ON_C * 10))
#define TEMP_OFF_DC   ((int16_t)((COOLANT_PUMP_TEMP_ON_C - COOLANT_PUMP_TEMP_HYST_C) * 10))

HAL_StatusTypeDef CoolantPump_Init(void)
{
    g_mutex = osMutexNew(&g_mutex_attr);
    if (g_mutex == NULL) return HAL_ERROR;
    memset(&g_cp, 0, sizeof(g_cp));
    return HAL_OK;
}

bool CoolantPump_GetDesiredPump(void)
{
    bool desired = false;
    if (g_mutex == NULL) return false;
    /* Startup lockout: hold the pump OFF for the first N ms after boot to
     * avoid stacking pump inrush on top of the rest of the system powering
     * up. Applies regardless of coolant temperature or RTD status. */
    if (osKernelGetTickCount() < (uint32_t)COOLANT_PUMP_STARTUP_LOCKOUT_MS) {
        return false;
    }
    if (osMutexAcquire(g_mutex, osWaitForever) == osOK) {
        desired = g_cp.temp_override || g_cp.rtd_active;
        osMutexRelease(g_mutex);
    }
    return desired;
}

/* ------------------------------------------------------------------------ */
/* Inverter Temperatures_3 (standard 11-bit, ID 0x0A2)                       */
/* ------------------------------------------------------------------------ */

bool CoolantPump_MatchInverterTemps(const CAN_Message_t *msg)
{
    return (msg != NULL) && (msg->id == INV_TEMPERATURES_3_ID);
}

void CoolantPump_HandleInverterTemps(const CAN_Message_t *msg)
{
    if (msg == NULL || msg->length < 2U) return;
    /* INV_Coolant_Temp : 0|16@1- (0.1 degC/LSB) */
    int16_t temp_dC = (int16_t)((uint16_t)msg->data[0] |
                                ((uint16_t)msg->data[1] << 8));
    if (osMutexAcquire(g_mutex, osWaitForever) == osOK) {
        g_cp.coolant_temp_dC = temp_dC;
        g_cp.temp_valid      = true;
        if (temp_dC >= TEMP_ON_DC) {
            g_cp.temp_override = true;
        } else if (temp_dC <= TEMP_OFF_DC) {
            g_cp.temp_override = false;
        }
        /* Between TEMP_OFF_DC and TEMP_ON_DC: hold previous state. */
        osMutexRelease(g_mutex);
    }
}

/* ------------------------------------------------------------------------ */
/* VCU_Summary (extended 29-bit, ID 0x8D0AAB0)                               */
/* ------------------------------------------------------------------------ */

bool CoolantPump_MatchVcuSummary(const CAN_Message_t *msg)
{
    return (msg != NULL) && (msg->id == VCU_SUMMARY_ID);
}

void CoolantPump_HandleVcuSummary(const CAN_Message_t *msg)
{
    if (msg == NULL || msg->length < 5U) return;
    /* VCU_RTD_Active : bit 32 (byte 4, bit 0) */
    bool rtd = (msg->data[4] & 0x01U) != 0U;
    if (osMutexAcquire(g_mutex, osWaitForever) == osOK) {
        g_cp.rtd_active = rtd;
        osMutexRelease(g_mutex);
    }
}
