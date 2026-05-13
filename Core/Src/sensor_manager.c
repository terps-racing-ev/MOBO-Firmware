/**
  ******************************************************************************
  * @file    sensor_manager.c
  ******************************************************************************
  */

#include "sensor_manager.h"
#include "config_manager.h"
#include "error_manager.h"
#include "watchdog.h"
#include <stdint.h>
#include <string.h>

extern ADC_HandleTypeDef hadc1;

#define SENSOR_VREF_MV         3300U
#define SENSOR_ADC_FULL        4095U
#define LV_CURR_AVG_SAMPLES    16U
#define LV_CURR_ZERO_RAW       2048U
#define LV_CURR_OFFSET_MA      200
/* Channel definitions per main.h CubeMX */
#define SENSOR_ADC_CH_BATT     ADC_CHANNEL_5
#define SENSOR_ADC_CH_BRAKE    ADC_CHANNEL_6
#define SENSOR_ADC_CH_5V       ADC_CHANNEL_7
#define SENSOR_ADC_CH_LV       ADC_CHANNEL_10
#define SENSOR_ADC_CH_HC       ADC_CHANNEL_11

static Sensor_Readings_t g_readings = {0};
static osMutexId_t       g_mutex    = NULL;

static const osMutexAttr_t g_mutex_attr = { .name = "SensorMutex" };

static HAL_StatusTypeDef ADC_Read_Channel(uint32_t channel, uint32_t *adc_raw)
{
    ADC_ChannelConfTypeDef sConfig = {0};

    if (adc_raw == NULL) {
        return HAL_ERROR;
    }

    sConfig.Channel = channel;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_640CYCLES_5;
    sConfig.SingleDiff = ADC_SINGLE_ENDED;
    sConfig.OffsetNumber = ADC_OFFSET_NONE;
    sConfig.Offset = 0;

    if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) {
        return HAL_ERROR;
    }

    if (HAL_ADC_Start(&hadc1) != HAL_OK) {
        return HAL_ERROR;
    }

    if (HAL_ADC_PollForConversion(&hadc1, 10) != HAL_OK) {
        HAL_ADC_Stop(&hadc1);
        return HAL_ERROR;
    }

    *adc_raw = HAL_ADC_GetValue(&hadc1);

    if (HAL_ADC_Stop(&hadc1) != HAL_OK) {
        return HAL_ERROR;
    }

    return HAL_OK;
}

static HAL_StatusTypeDef Sensor_ReadLcSummaryLegacy(Sensor_Readings_t *local)
{
    uint32_t five_v_raw = 0;
    uint32_t brake_raw = 0;
    uint32_t lv_curr_raw = 0;
    uint32_t lv_curr_sum = 0;
    int32_t lv_curr_delta_raw = 0;

    uint32_t five_v_mv = 0;
    uint32_t brake_mv = 0;
    uint32_t lv_curr_mv = 0;
    int32_t lv_curr_a_milli = 0;
    uint32_t sample_index = 0;

    if (ADC_Read_Channel(ADC_CHANNEL_7, &five_v_raw) != HAL_OK) {
        return HAL_ERROR;
    }

    if (ADC_Read_Channel(ADC_CHANNEL_6, &brake_raw) != HAL_OK) {
        return HAL_ERROR;
    }

    for (sample_index = 0; sample_index < LV_CURR_AVG_SAMPLES; sample_index++) {
        uint32_t sample_raw = 0;

        if (ADC_Read_Channel(SENSOR_ADC_CH_LV, &sample_raw) != HAL_OK) {
            return HAL_ERROR;
        }

        lv_curr_sum += sample_raw;
    }

    lv_curr_raw = lv_curr_sum / LV_CURR_AVG_SAMPLES;

    five_v_mv = (five_v_raw * 3300U * 2) / 4095U;
    brake_mv = (brake_raw * 3300U * 2) / 4095U;
    lv_curr_mv = (lv_curr_raw * 3300U) / 4095U;
    (void)lv_curr_mv;
    lv_curr_delta_raw = (int32_t)lv_curr_raw - (int32_t)LV_CURR_ZERO_RAW;

    lv_curr_a_milli = (int32_t)((((int64_t)lv_curr_delta_raw * 3300LL * 1000LL) / 4095LL) / 44LL);

    if (lv_curr_a_milli > 0) {
        lv_curr_a_milli += LV_CURR_OFFSET_MA;
    }

    if (lv_curr_a_milli <= 200) {
        lv_curr_a_milli = 0;
    }

    if (lv_curr_a_milli > INT16_MAX) {
        lv_curr_a_milli = INT16_MAX;
    }
    if (lv_curr_a_milli < INT16_MIN) {
        lv_curr_a_milli = INT16_MIN;
    }

    local->five_v_mv = (uint16_t)five_v_mv;
    local->brake_mv = (uint16_t)brake_mv;
    local->lv_current_ma = (int16_t)lv_curr_a_milli;
    local->lv_current_raw = (uint16_t)lv_curr_raw;
    return HAL_OK;
}

static HAL_StatusTypeDef Sensor_ReadHcSummaryLegacy(Sensor_Readings_t *local)
{
    uint32_t hc_curr_raw = 0;
    uint32_t hc_curr_sum = 0;
    int32_t hc_curr_delta_raw = 0;
    int32_t hc_curr_a_milli = 0;
    uint32_t sample_index = 0;

    for (sample_index = 0; sample_index < LV_CURR_AVG_SAMPLES; sample_index++) {
        uint32_t sample_raw = 0;

        if (ADC_Read_Channel(ADC_CHANNEL_11, &sample_raw) != HAL_OK) {
            return HAL_ERROR;
        }

        hc_curr_sum = hc_curr_sum + sample_raw;
    }

    hc_curr_raw = hc_curr_sum / LV_CURR_AVG_SAMPLES;
    hc_curr_delta_raw = (int32_t)hc_curr_raw - (int32_t)LV_CURR_ZERO_RAW;

    hc_curr_a_milli = (int32_t)((((int64_t)hc_curr_delta_raw * 3300LL * 1000LL) / 4095LL) / 44LL);

    if (hc_curr_a_milli > 0) {
        hc_curr_a_milli += LV_CURR_OFFSET_MA;
    }

    if (hc_curr_a_milli <= 200) {
        hc_curr_a_milli = 0;
    }

    if (hc_curr_a_milli > INT16_MAX) {
        hc_curr_a_milli = INT16_MAX;
    }
    if (hc_curr_a_milli < INT16_MIN) {
        hc_curr_a_milli = INT16_MIN;
    }

    local->hc_current_ma = (int16_t)hc_curr_a_milli;
    return HAL_OK;
}

HAL_StatusTypeDef Sensor_Init(void)
{
    g_mutex = osMutexNew(&g_mutex_attr);
    if (g_mutex == NULL) return HAL_ERROR;
    memset(&g_readings, 0, sizeof(g_readings));
    /* HAL_ADCEx_Calibration_Start is invoked once in main.c after MX_ADC1_Init. */
    return HAL_OK;
}

void Sensor_GetReadings(Sensor_Readings_t *out)
{
    if (out == NULL) return;
    if (osMutexAcquire(g_mutex, osWaitForever) == osOK) {
        *out = g_readings;
        osMutexRelease(g_mutex);
    }
}

void Sensor_ClearPeaks(void)
{
    if (osMutexAcquire(g_mutex, osWaitForever) == osOK) {
        g_readings.lv_current_peak_ma = 0;
        g_readings.hc_current_peak_ma = 0;
        osMutexRelease(g_mutex);
    }
}

void SensorTask(void *argument)
{
    (void)argument;
    osDelay(20);

    for (;;) {
        Watchdog_Heartbeat(WD_TASK_SENSOR);

        Sensor_Readings_t local;
        memset(&local, 0, sizeof(local));
        local.last_update_tick = osKernelGetTickCount();
        bool any_fail = false;

        uint32_t raw = 0;
        uint32_t vdiv_num = Config_GetVoltageDividerNum();
        uint32_t vdiv_den = Config_GetVoltageDividerDen();

        if (ADC_Read_Channel(SENSOR_ADC_CH_BATT, &raw) == HAL_OK) {
            uint32_t mv = (raw * SENSOR_VREF_MV * vdiv_num)
                          / ((uint32_t)SENSOR_ADC_FULL * vdiv_den);
            if (mv > 0xFFFFU) mv = 0xFFFFU;
            local.battery_mv = (uint16_t)mv;
        } else {
            any_fail = true;
        }

        if (Sensor_ReadLcSummaryLegacy(&local) != HAL_OK) {
            any_fail = true;
        }

        if (Sensor_ReadHcSummaryLegacy(&local) != HAL_OK) {
            any_fail = true;
        }

        if (any_fail) {
            ErrorMgr_SetError(ERROR_ADC_FAULT);
        } else {
            ErrorMgr_ClearError(ERROR_ADC_FAULT);
        }

        if (osMutexAcquire(g_mutex, osWaitForever) == osOK) {
            int16_t lv_peak = (local.lv_current_ma > g_readings.lv_current_peak_ma)
                              ? local.lv_current_ma : g_readings.lv_current_peak_ma;
            int16_t hc_peak = (local.hc_current_ma > g_readings.hc_current_peak_ma)
                              ? local.hc_current_ma : g_readings.hc_current_peak_ma;
            local.lv_current_peak_ma = lv_peak;
            local.hc_current_peak_ma = hc_peak;
            local.fault_flags = any_fail ? 1U : 0U;
            g_readings = local;
            osMutexRelease(g_mutex);
        }

        osDelay(SENSOR_TASK_PERIOD_MS);
    }
}
