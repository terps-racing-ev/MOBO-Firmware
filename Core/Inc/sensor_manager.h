/**
  ******************************************************************************
  * @file    sensor_manager.h
  * @brief   ADC1 owner: battery voltage, 5V rail, brake input, LV/HC currents
  *
  * Sole owner of ADC1. Caches the last configured channel to avoid the
  * per-read reconfiguration overhead the legacy code suffered from. Current
  * channels (LV, HC) are 16-sample averaged with calibration offset and noise
  * floor sourced from config_manager.
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

#define SENSOR_TASK_PERIOD_MS  50U
#define SENSOR_CURRENT_AVG_N   16U
#define SENSOR_CURRENT_NOISE_FLOOR_MA  200

/** Latest sensor readings. All fields updated atomically each task tick. */
typedef struct {
    uint16_t battery_mv;        /**< Battery rail in mV */
    uint16_t five_v_mv;         /**< 5 V rail in mV */
    uint16_t brake_mv;          /**< Brake input in mV */
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
