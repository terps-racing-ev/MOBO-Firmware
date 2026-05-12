#include "drivers/hc_control.h"
#include "main.h"

static uint8_t rpi_override = 0U;

void HC_Control_HandleRpiCommand(CAN_Message_t *message) {
    if ((message == NULL) || (message->id != RPI_MOBO_Command_ID)) {
        return;
    }

    if ((message->data[0] & 0x01U) != 1U) {
        return;
    }

    if ((message->length > 0U) && (message->data[0] == 0x01U)) {
        rpi_override = 1U;
    }

    if (message->length > 1U) {
        HAL_GPIO_WritePin(PUMP_Ctrl_GPIO_Port, PUMP_Ctrl_Pin,
                          ((message->data[1] >> 0) & 0x01U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
        HAL_GPIO_WritePin(DRS_Ctrl_GPIO_Port, DRS_Ctrl_Pin,
                          ((message->data[1] >> 1) & 0x01U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
        HAL_GPIO_WritePin(FANS_Ctrl_GPIO_Port, FANS_Ctrl_Pin,
                          ((message->data[1] >> 2) & 0x01U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
        HAL_GPIO_WritePin(RAD_Ctrl_GPIO_Port, RAD_Ctrl_Pin,
                          ((message->data[1] >> 3) & 0x01U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    }
}

void HC_Control_HandleVcuCommand(CAN_Message_t *message) {
    if ((message == NULL) || (message->id != VCU_MOBO_Command_ID)) {
        return;
    }

    if (rpi_override != 0U) {
        return;
    }

    if ((message->data[0] & 0x01U) != 1U) {
        return;
    }

    if (message->length > 1U) {
        HAL_GPIO_WritePin(PUMP_Ctrl_GPIO_Port, PUMP_Ctrl_Pin,
                          ((message->data[1] >> 0) & 0x01U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
        HAL_GPIO_WritePin(DRS_Ctrl_GPIO_Port, DRS_Ctrl_Pin,
                          ((message->data[1] >> 1) & 0x01U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
        HAL_GPIO_WritePin(FANS_Ctrl_GPIO_Port, FANS_Ctrl_Pin,
                          ((message->data[1] >> 2) & 0x01U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
        HAL_GPIO_WritePin(RAD_Ctrl_GPIO_Port, RAD_Ctrl_Pin,
                          ((message->data[1] >> 3) & 0x01U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    }
}
