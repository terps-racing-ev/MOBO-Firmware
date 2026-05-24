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

#define SENSOR_VREF_MV            3300U
#define SENSOR_ADC_FULL           4095U
/* ACS37012 mid-rail bias (datasheet: VCC/2 nominal). We assume an ideal 3.3 V
 * rail; the corresponding raw count is the zero-current reference. */
#define SENSOR_CURRENT_ZERO_MV    (SENSOR_VREF_MV / 2U)
#define SENSOR_CURRENT_ZERO_RAW   ((SENSOR_CURRENT_ZERO_MV * SENSOR_ADC_FULL) / SENSOR_VREF_MV)
/* ACS37012LLZATR-030B3 sensitivity (datasheet): 44 mV / A. */
#define ACS37012_MV_PER_A         44

/* Channel definitions per main.h CubeMX */
#define SENSOR_ADC_CH_BATT     ADC_CHANNEL_5
#define SENSOR_ADC_CH_BRAKE    ADC_CHANNEL_6
#define SENSOR_ADC_CH_5V       ADC_CHANNEL_7
#define SENSOR_ADC_CH_LV       ADC_CHANNEL_10
#define SENSOR_ADC_CH_HC       ADC_CHANNEL_11

/* Rolling average over SENSOR_CURRENT_AVG_DEPTH task ticks (2 s @ 50 ms).
 * Used for all analog channels that benefit from low-pass filtering on top
 * of the ADC's hardware 256x oversampling (current sensors and battery). */
typedef struct {
    uint32_t buf[SENSOR_CURRENT_AVG_DEPTH];
    uint64_t sum;
    uint16_t idx;
    uint16_t count;
} SampleAvg_t;

static Sensor_Readings_t g_readings = {0};
static osMutexId_t       g_mutex    = NULL;
static SampleAvg_t       g_lv_avg   = {0};
static SampleAvg_t       g_hc_avg   = {0};
static SampleAvg_t       g_batt_avg = {0};

static const osMutexAttr_t g_mutex_attr = { .name = "SensorMutex" };

static uint32_t SampleAvg_Push(SampleAvg_t *a, uint32_t raw)
{
    if (a->count < SENSOR_CURRENT_AVG_DEPTH) {
        a->buf[a->idx] = raw;
        a->sum += raw;
        a->count++;
    } else {
        a->sum -= a->buf[a->idx];
        a->buf[a->idx] = raw;
        a->sum += raw;
    }
    a->idx++;
    if (a->idx >= SENSOR_CURRENT_AVG_DEPTH) {
        a->idx = 0;
    }
    return (uint32_t)(a->sum / a->count);
}

static int16_t Sensor_RawToCurrentMa(uint32_t avg_raw)
{
    int32_t delta = (int32_t)avg_raw - (int32_t)SENSOR_CURRENT_ZERO_RAW;
    /* mA = delta_raw * VREF_mV * 1000 / (ADC_FULL * sensitivity_mV_per_A) */
    int64_t num = (int64_t)delta * (int64_t)SENSOR_VREF_MV * 1000LL;
    int64_t den = (int64_t)SENSOR_ADC_FULL * (int64_t)ACS37012_MV_PER_A;
    int64_t mA  = num / den;
    if (mA > INT16_MAX) mA = INT16_MAX;
    if (mA < INT16_MIN) mA = INT16_MIN;
    return (int16_t)mA;
}

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

static HAL_StatusTypeDef Sensor_ReadLcSummary(Sensor_Readings_t *local)
{
    uint32_t five_v_raw = 0;
    uint32_t brake_raw = 0;
    uint32_t lv_curr_raw = 0;

    if (ADC_Read_Channel(SENSOR_ADC_CH_5V, &five_v_raw) != HAL_OK) {
        return HAL_ERROR;
    }
    if (ADC_Read_Channel(SENSOR_ADC_CH_BRAKE, &brake_raw) != HAL_OK) {
        return HAL_ERROR;
    }
    if (ADC_Read_Channel(SENSOR_ADC_CH_LV, &lv_curr_raw) != HAL_OK) {
        return HAL_ERROR;
    }

    uint32_t five_v_mv = (five_v_raw * SENSOR_VREF_MV * 2U) / SENSOR_ADC_FULL;
    /* Brake sensor: original 0.5..4.5 V signal goes through an on-board
     * 10k/10k divider before reaching the ADC pin, so multiplying the pin
     * voltage by 2 recovers the original sensor voltage in mV. */
    uint32_t brake_mv  = (brake_raw  * SENSOR_VREF_MV * 2U) / SENSOR_ADC_FULL;

    /* Convert sensor voltage to brake pressure (PSI):
     *   500 mV -> 0 PSI, 4500 mV -> 3000 PSI  =>  PSI = (mV - 500) * 3/4.
     * Outside the +/-250 mV tolerance band (i.e. < 250 mV or > 4750 mV) the
     * sensor is considered disconnected/faulted and we publish 0 PSI.
     * Inside the tolerance band but outside the 500..4500 mV nominal range
         * we clamp to 0 or 3000 PSI. A small deadband suppresses anything below
         * 10 PSI to 0 PSI. */
    uint16_t brake_psi = 0;
    if (brake_mv >= 250U && brake_mv <= 4750U) {
        uint32_t mv_clamped = brake_mv;
        if (mv_clamped < 500U)  mv_clamped = 500U;
        if (mv_clamped > 4500U) mv_clamped = 4500U;
        brake_psi = (uint16_t)(((mv_clamped - 500U) * 3000U) / 4000U);
            if (brake_psi < 10U) {
                brake_psi = 0U;
            }
    }

    uint32_t lv_avg_raw = SampleAvg_Push(&g_lv_avg, lv_curr_raw);

    local->five_v_mv      = (uint16_t)((five_v_mv > 0xFFFFU) ? 0xFFFFU : five_v_mv);
    local->brake_psi      = brake_psi;
    local->lv_current_ma  = Sensor_RawToCurrentMa(lv_avg_raw);
    local->lv_current_raw = (uint16_t)lv_avg_raw;
    return HAL_OK;
}

static HAL_StatusTypeDef Sensor_ReadHcSummary(Sensor_Readings_t *local)
{
    uint32_t hc_curr_raw = 0;

    if (ADC_Read_Channel(SENSOR_ADC_CH_HC, &hc_curr_raw) != HAL_OK) {
        return HAL_ERROR;
    }

    uint32_t hc_avg_raw = SampleAvg_Push(&g_hc_avg, hc_curr_raw);
    local->hc_current_ma = Sensor_RawToCurrentMa(hc_avg_raw);
    return HAL_OK;
}

HAL_StatusTypeDef Sensor_Init(void)
{
    g_mutex = osMutexNew(&g_mutex_attr);
    if (g_mutex == NULL) return HAL_ERROR;
    memset(&g_readings, 0, sizeof(g_readings));
    memset(&g_lv_avg,   0, sizeof(g_lv_avg));
    memset(&g_hc_avg,   0, sizeof(g_hc_avg));
    memset(&g_batt_avg, 0, sizeof(g_batt_avg));

    /* Enable ADC hardware oversampling: 256 samples averaged on-chip per
     * HAL_ADC_Start, with right-shift 8 to keep the output in 12-bit range.
     * Combined with the 640.5-cycle per-sub-sample acquisition time set in
     * ADC_Read_Channel, this gives the deepest noise reduction the ADC
     * peripheral supports before any software averaging is layered on top. */
    hadc1.Init.OversamplingMode               = ENABLE;
    hadc1.Init.Oversampling.Ratio             = ADC_OVERSAMPLING_RATIO_256;
    hadc1.Init.Oversampling.RightBitShift     = ADC_RIGHTBITSHIFT_8;
    hadc1.Init.Oversampling.TriggeredMode     = ADC_TRIGGEREDMODE_SINGLE_TRIGGER;
    hadc1.Init.Oversampling.OversamplingStopReset = ADC_REGOVERSAMPLING_CONTINUED_MODE;
    if (HAL_ADC_Init(&hadc1) != HAL_OK) {
        return HAL_ERROR;
    }
    /* Re-calibrate after re-init; main.c's pre-RTOS calibration was for the
     * non-oversampled configuration. */
    if (HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED) != HAL_OK) {
        return HAL_ERROR;
    }
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
            /* Push the (already 256x HW-oversampled) raw count through the
             * 2 s rolling average, then apply the divider to get Vbatt. */
            uint32_t batt_avg_raw = SampleAvg_Push(&g_batt_avg, raw);
            uint64_t mv = ((uint64_t)batt_avg_raw * (uint64_t)SENSOR_VREF_MV * (uint64_t)vdiv_num)
                          / ((uint64_t)SENSOR_ADC_FULL * (uint64_t)vdiv_den);
            if (mv > 0xFFFFU) mv = 0xFFFFU;
            local.battery_mv = (uint16_t)mv;
        } else {
            any_fail = true;
        }

        if (Sensor_ReadLcSummary(&local) != HAL_OK) {
            any_fail = true;
        }

        if (Sensor_ReadHcSummary(&local) != HAL_OK) {
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
