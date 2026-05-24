/**
  ******************************************************************************
  * @file    sensor_manager.h
  * @brief   ADC1 owner: battery voltage, 5V rail, brake input, LV/HC currents
  *
  * Sole owner of ADC1. Current sensors (LV, HC) use ADC hardware oversampling
  * (256x averaged on-chip, max sampling time per sub-sample) combined with a
  * 2-second software rolling average to suppress noise. The zero-current
  * point is derived from the assumed 1.65 V mid-rail bias of the ACS37012
  * (no hard-coded raw constant, no per-board offset).
  ******************************************************************************
  */

#ifndef __SENSOR_MANAGER_H
#define __SENSOR_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "cmsis_os.h"
#include <stdint.h>

#define SENSOR_TASK_PERIOD_MS         50U
#define SENSOR_CURRENT_AVG_WINDOW_MS  2000U
#define SENSOR_CURRENT_AVG_DEPTH      (SENSOR_CURRENT_AVG_WINDOW_MS / SENSOR_TASK_PERIOD_MS)

/** Latest sensor readings. All fields updated atomically each task tick. */
typedef struct {
    uint16_t battery_mv;        /**< Battery rail in mV */
    uint16_t five_v_mv;         /**< 5 V rail in mV */
    uint16_t brake_psi;         /**< Brake pressure in PSI (0..3000); 0 when sensor reading is out of valid range */
    int16_t  lv_current_ma;     /**< LV current in mA (signed, after cal/noise) */
    int16_t  hc_current_ma;     /**< HC current in mA */
    int16_t  lv_current_peak_ma;
    int16_t  hc_current_peak_ma;
    uint16_t lv_current_raw;    /**< Raw ADC count for LV current channel */
    uint32_t last_update_tick;
    uint8_t  fault_flags;       /**< Bit0: ADC HAL_ERROR */
} Sensor_Readings_t;

HAL_StatusTypeDef Sensor_Init(void);
void              SensorTask(void *argument);
void              Sensor_GetReadings(Sensor_Readings_t *out);
void              Sensor_ClearPeaks(void);

#ifdef __cplusplus
}
#endif

#endif /* __SENSOR_MANAGER_H */
