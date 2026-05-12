#include "managers/can_manager.h"
#include <string.h>

HAL_StatusTypeDef CAN_Send_Message(uint32_t id, uint8_t *data, uint8_t length, uint8_t priority) {
    CAN_Message_t message;

    if (length > 8 || id > 0x1FFFFFFF) {
        return HAL_ERROR;
    }

    message.id = id;
    message.length = length;
    message.priority = priority;
    message.timestamp = osKernelGetTickCount();

    if (data != NULL && length > 0) {
        memcpy(message.data, data, length);
    }

    if (osMessageQueuePut(canTxQueueHandle, &message, priority, CAN_TX_TIMEOUT_MS) != osOK) {
        canStats.tx_queue_full_count++;
        return HAL_ERROR;
    }

    return HAL_OK;
}
