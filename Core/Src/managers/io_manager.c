#include "managers/io_manager.h"
#include "data/can.h"
#include "main.h"
#include "cmsis_os.h"

static HAL_StatusTypeDef ADC_Read_Channel(uint32_t channel, uint32_t *adc_raw);
static HAL_StatusTypeDef IO_Send_MOBO_Summary(void);
static HAL_StatusTypeDef IO_Send_Power_Info(void);
static HAL_StatusTypeDef IO_Send_MOBO_LC_Summary(void);
static HAL_StatusTypeDef IO_Send_MOBO_HC_Summary(void);

extern ADC_HandleTypeDef hadc1;

void IO_ManagerTask(void *argument) {
    (void)argument;

    osDelay(100);

    for (;;) {
        (void)IO_Send_MOBO_Summary();
        (void)IO_Send_Power_Info();
        (void)IO_Send_MOBO_LC_Summary();
        (void)IO_Send_MOBO_HC_Summary();

        osDelay(IO_REFRESH_FREQ_MS);
    }
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

static HAL_StatusTypeDef IO_Send_MOBO_Summary(void) {
    uint8_t summary_data[8] = {0};
    uint32_t tick = osKernelGetTickCount();


    //Byte 1 summary making. 
    GPIO_PinState sdc_1;
    GPIO_PinState sdc_2;
    GPIO_PinState sdc_3;
    GPIO_PinState bms;
    GPIO_PinState bspd; 
    GPIO_PinState imd;


    sdc_1 = HAL_GPIO_ReadPin(SDC_1_GPIO_Port, SDC_1_Pin);
    sdc_2 = HAL_GPIO_ReadPin(SDC_2_GPIO_Port, SDC_2_Pin);
    sdc_3 = HAL_GPIO_ReadPin(SDC_3_GPIO_Port, SDC_3_Pin);
    bms = HAL_GPIO_ReadPin(BMS_GPIO_Port, BMS_Pin);
    bspd = HAL_GPIO_ReadPin(BSPD_GPIO_Port, BSPD_Pin);
    imd = HAL_GPIO_ReadPin(IMD_GPIO_Port, IMD_Pin);

 
    summary_data[1] = 0;
    // These safety inputs use pull-ups, so an asserted/active signal reads low.
    summary_data[1] |= ((sdc_1 == GPIO_PIN_RESET) ? 1U : 0U) << 0;
    summary_data[1] |= ((sdc_2 == GPIO_PIN_RESET) ? 1U : 0U) << 1;
    summary_data[1] |= ((sdc_3 == GPIO_PIN_RESET) ? 1U : 0U) << 2;
    summary_data[1] |= ((bms == GPIO_PIN_RESET) ? 1U : 0U) << 3;
    summary_data[1] |= ((bspd == GPIO_PIN_RESET) ? 1U : 0U) << 4;
    summary_data[1] |= ((imd == GPIO_PIN_RESET) ? 1U : 0U) << 5;

    // Byte 0 live status summary.
    // bit 0: heartbeat toggle
    // bit 1: TX success counter LSB
    // bit 2: RX message counter LSB
    // bit 3: TX queue has data
    // bit 4: RX queue has data
    // bit 5: TX queue depth >= 2
    // bit 6: RX queue depth >= 2
    // bit 7: reserved
    summary_data[0] = 0;
    summary_data[0] |= (uint8_t)((tick / CAN_HEARTBEAT_INTERVAL_MS) & 0x01U) << 0;
    //summary_data[0] |= (uint8_t)(canStats.tx_success_count & 0x01U) << 1;
    //summary_data[0] |= (uint8_t)(canStats.rx_message_count & 0x01U) << 2;
    //summary_data[0] |= ((txQueueCount > 0U) ? 1U : 0U) << 3;
    //summary_data[0] |= ((rxQueueCount > 0U) ? 1U : 0U) << 4;
    //summary_data[0] |= ((txQueueCount > 1U) ? 1U : 0U) << 5;
    //summary_data[0] |= ((rxQueueCount > 1U) ? 1U : 0U) << 6;

    return CAN_Send_Message(CAN_MOBO_Summary_ID, summary_data, 8, CAN_PRIORITY_NORMAL);

}

static HAL_StatusTypeDef IO_Send_Power_Info(void){
    uint8_t batt_voltage_data[8] = {0};

    uint32_t adc_raw = 0;
    uint32_t batt_mv = 0;

    if (ADC_Read_Channel(ADC_CHANNEL_5, &adc_raw) != HAL_OK) {
        return HAL_ERROR;
    }

    batt_mv = (adc_raw * 330U * 92U) / (4095U);

    batt_voltage_data[0] = (uint8_t)(batt_mv & 0xFF);
    batt_voltage_data[1] = (uint8_t)((batt_mv >> 8) & 0xFF);
    //batt_voltage_data[2] = (uint8_t)(adc_raw & 0xFF);
    //batt_voltage_data[3] = (uint8_t)((adc_raw >> 8) & 0xFF);

    return CAN_Send_Message(CAN_MOBO_Power_Info_ID, batt_voltage_data, 8, CAN_PRIORITY_NORMAL);
}

static HAL_StatusTypeDef IO_Send_MOBO_LC_Summary(void){
    uint8_t lc_summary_data[8] = {0};

    uint32_t five_v_raw = 0;
    uint32_t brake_raw = 0;
    uint32_t lv_curr_raw = 0;
    uint32_t lv_curr_sum = 0;
    int32_t lv_curr_delta_raw = 0;

    uint32_t five_v_mv = 0;
    uint32_t brake_mv = 0;
    uint32_t lv_curr_mv = 0;
    int16_t lv_curr_a_milli = 0;
    uint32_t sample_index = 0;

    if (ADC_Read_Channel(ADC_CHANNEL_7, &five_v_raw) != HAL_OK) {
        return HAL_ERROR;
    }

    if (ADC_Read_Channel(ADC_CHANNEL_6, &brake_raw) != HAL_OK) {
        return HAL_ERROR;
    }

    for (sample_index = 0; sample_index < LV_CURR_AVG_SAMPLES; sample_index++) {
        uint32_t sample_raw = 0;

        if (ADC_Read_Channel(ADC_CHANNEL_8, &sample_raw) != HAL_OK) {
            return HAL_ERROR;
        }

        lv_curr_sum += sample_raw;
    }

    lv_curr_raw = lv_curr_sum / LV_CURR_AVG_SAMPLES;

    five_v_mv = (five_v_raw * 3300U * 2) / 4095U;
    brake_mv = (brake_raw * 3300U * 2) / 4095U;
    lv_curr_mv = (lv_curr_raw * 3300U) / 4095U;
    lv_curr_delta_raw = (int32_t)lv_curr_raw - (int32_t)LV_CURR_ZERO_RAW;

    // ACS37012LLZATR-030B3 nominal conversion:
    // 44 mV/A sensitivity, zero centered near raw count 2048.
    // Send current in amps with 0.001 A resolution.
    // Example: 12.345 A is transmitted as 12345.
    lv_curr_a_milli = (int16_t)((((int64_t)lv_curr_delta_raw * 3300LL * 1000LL) / 4095LL) / 44LL);

    // Empirical trim: previous version was consistently about 0.2 A low.
    if (lv_curr_a_milli > 0) {
        lv_curr_a_milli += LV_CURR_OFFSET_MA;
    }

    // Suppress near-zero noise/jitter from the sensor + ADC path.
    if (lv_curr_a_milli <= 200) {
        lv_curr_a_milli = 0;
    }

    lc_summary_data[0] = (uint8_t)(five_v_mv & 0xFF);
    lc_summary_data[1] = (uint8_t)((five_v_mv >> 8) & 0xFF);
    lc_summary_data[2] = (uint8_t)(brake_mv & 0xFF);
    lc_summary_data[3] = (uint8_t)((brake_mv >> 8) & 0xFF);
    lc_summary_data[4] = (uint8_t)((uint16_t)lv_curr_a_milli & 0xFF);
    lc_summary_data[5] = (uint8_t)(((uint16_t)lv_curr_a_milli >> 8) & 0xFF);
    //lc_summary_data[6] = (uint8_t)(lv_curr_raw & 0xFF);
    //lc_summary_data[7] = (uint8_t)((lv_curr_raw >> 8) & 0xFF);

    return CAN_Send_Message(CAN_MOBO_LC_Summary_ID, lc_summary_data, 8, CAN_PRIORITY_NORMAL);
}

static HAL_StatusTypeDef IO_Send_MOBO_HC_Summary(void){
    uint8_t hc_summary_data[8] = {0};
    
    uint32_t hc_curr_raw = 0;
    uint32_t hc_curr_sum = 0;
    int32_t hc_curr_delta_raw = 0;
    int16_t hc_curr_a_milli = 0;
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

    hc_curr_a_milli = (int16_t)((((int64_t)hc_curr_delta_raw * 3300LL * 1000LL) / 4095LL) / 44LL);

    if (hc_curr_a_milli > 0) {
        hc_curr_a_milli += LV_CURR_OFFSET_MA;
    }

    if (hc_curr_a_milli <= 200) {
        hc_curr_a_milli = 0;
    }

    hc_summary_data[0] = (uint8_t)((uint16_t)hc_curr_a_milli & 0xFF);
    hc_summary_data[1] = (uint8_t)(((uint16_t)hc_curr_a_milli >> 8) & 0xFF);
    //hc_summary_data[2] = (uint8_t)(hc_curr_raw & 0xFF);
    //hc_summary_data[3] = (uint8_t)((hc_curr_raw >> 8) & 0xFF);
    
    GPIO_PinState fans;
    GPIO_PinState pump;
    GPIO_PinState drs;
    GPIO_PinState rad;

    fans = HAL_GPIO_ReadPin(FANS_Ctrl_GPIO_Port, FANS_Ctrl_Pin);
    pump = HAL_GPIO_ReadPin(PUMP_Ctrl_GPIO_Port, PUMP_Ctrl_Pin);
    drs = HAL_GPIO_ReadPin(DRS_Ctrl_GPIO_Port, DRS_Ctrl_Pin);
    rad = HAL_GPIO_ReadPin(RAD_Ctrl_GPIO_Port, RAD_Ctrl_Pin);

    hc_summary_data[2] = 0;
    hc_summary_data[2] |= ((pump == GPIO_PIN_SET) ? 1U : 0U) << 0;
    hc_summary_data[2] |= ((drs == GPIO_PIN_SET) ? 1U : 0U) << 1;
    hc_summary_data[2] |= ((fans == GPIO_PIN_SET) ? 1U : 0U) << 2;
    hc_summary_data[2] |= ((rad == GPIO_PIN_SET) ? 1U : 0U) << 3;

    return CAN_Send_Message(CAN_MOBO_HC_Summary_ID, hc_summary_data, 8, CAN_PRIORITY_NORMAL);
}
