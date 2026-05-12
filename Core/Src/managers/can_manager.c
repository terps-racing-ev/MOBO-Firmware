#define CAN_MANAGER_IMPLEMENTATION
#include "managers/can_manager.h"
#include "main.h"

osMessageQueueId_t canTxQueueHandle = NULL;
osMessageQueueId_t canRxQueueHandle = NULL;

static void CAN_ProcessTXQueue(void);
static void CAN_ProcessRXQueue(CAN_Message_t *message);
static HAL_StatusTypeDef CAN_Transmit(CAN_Message_t *message);

CAN_Stats_t canStats = {0};

HAL_StatusTypeDef CAN_Init(void) {
    canTxQueueHandle = osMessageQueueNew(CAN_TX_QUEUE_SIZE, sizeof(CAN_Message_t), NULL);
    canRxQueueHandle = osMessageQueueNew(CAN_RX_QUEUE_SIZE, sizeof(CAN_Message_t), NULL);

    if (canTxQueueHandle == NULL || canRxQueueHandle == NULL) {
        return HAL_ERROR;
    }

    CAN_Reset_Stats(&canStats);
    return HAL_OK;
}

void CAN_ManagerTask(void *argument) {
    CAN_Message_t rxMessage;

    osDelay(100);

    for (;;) {
        while (osMessageQueueGet(canRxQueueHandle, &rxMessage, NULL, 0) == osOK) {
            CAN_ProcessRXQueue(&rxMessage);
        }

        CAN_ProcessTXQueue();

        osDelay(10);
    }
}

static void CAN_ProcessTXQueue(void) {
    CAN_Message_t message;

    while (osMessageQueueGet(canTxQueueHandle, &message, NULL, 0) == osOK) {
        if (CAN_Transmit(&message) != HAL_OK) {
            canStats.tx_failure_count++;
            break;
        }
    }
}


static void CAN_ProcessRXQueue(CAN_Message_t *message) {
    uint32_t handlerIndex = 0;

    if (message == NULL) {
        return;
    }

    canStats.rx_message_count++;

    for (handlerIndex = 0; handlerIndex < canRxHandlerCount; handlerIndex++) {
        canRxHandlers[handlerIndex](message);
    }
}

static HAL_StatusTypeDef CAN_Transmit(CAN_Message_t *message) {
    CAN_TxHeaderTypeDef txHeader;
    uint32_t txMailbox;
    HAL_StatusTypeDef status;
    uint8_t retryCount = 0;

    if (message == NULL) {
        return HAL_ERROR;
    }

    txHeader.ExtId = message->id;
    txHeader.StdId = 0;
    txHeader.RTR = CAN_RTR_DATA;
    txHeader.IDE = CAN_ID_EXT;
    txHeader.DLC = message->length;
    txHeader.TransmitGlobalTime = DISABLE;

    while (retryCount < CAN_MAX_RETRIES) {
        status = HAL_CAN_AddTxMessage(&hcan1, &txHeader, message->data, &txMailbox);

        if (status == HAL_OK) {
            canStats.tx_success_count++;
            return HAL_OK;
        }

        retryCount++;

        if (retryCount < CAN_MAX_RETRIES) {
            osDelay(1);
        }
    }

    canStats.bus_error_count++;
    return HAL_ERROR;
}


void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan) {
    CAN_RxHeaderTypeDef RxHeader;
    CAN_Message_t msg;

    if ((hcan == NULL) || (canRxQueueHandle == NULL)) {
        return;
    }

    if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &RxHeader, msg.data) == HAL_OK) {
        msg.id = RxHeader.ExtId;
        msg.length = RxHeader.DLC;
        msg.priority = 0;
        msg.timestamp = osKernelGetTickCount();

        if (osMessageQueuePut(canRxQueueHandle, &msg, 0, 0) != osOK) {
            canStats.rx_queue_full_count++;
        }
    }
}

void CAN_Get_Stats(CAN_Stats_t *stats) {
    if (stats != NULL) {
        memcpy(stats, &canStats, sizeof(CAN_Stats_t));
    }
}

void CAN_Reset_Stats(CAN_Stats_t *stats) {
    if (stats != NULL) {
        memset(stats, 0, sizeof(CAN_Stats_t));
    }
}

uint32_t CAN_GetTXQueueCount(void) {
    if (canTxQueueHandle == NULL) {
        return 0;
    }
    return osMessageQueueGetCount(canTxQueueHandle);
}

uint32_t CAN_GetRXQueueCount(void) {
    if (canRxQueueHandle == NULL) {
        return 0;
    }
    return osMessageQueueGetCount(canRxQueueHandle);
}

uint32_t CAN_Flush_TX_Queue(void) {
    CAN_Message_t dummyMessage;
    uint32_t count = 0;

    if (canTxQueueHandle == NULL) {
        return 0;
    }

    while (osMessageQueueGet(canTxQueueHandle, &dummyMessage, NULL, 0) == osOK) {
        count++;
    }

    return count;
}

/*
HAL_StatusTypeDef CAN_SendHeartbeat(void) {
    uint8_t heartbeat_data[8] = {0};

    uint32_t tick = osKernelGetTickCount();

    heartbeat_data[0] = (uint8_t)(tick & 0xFF);
    heartbeat_data[1] = (uint8_t)((tick >> 8) & 0xFF);
    heartbeat_data[2] = (uint8_t)((tick >> 16) & 0xFF);
    heartbeat_data[3] = (uint8_t)((tick >> 24) & 0xFF);

    heartbeat_data[4] = (uint8_t)(canStats.rx_message_count & 0xFF);
    heartbeat_data[5] = (uint8_t)(canStats.tx_success_count & 0xFF);
    heartbeat_data[6] = (uint8_t)(CAN_GetTXQueueCount() & 0xFF);
    heartbeat_data[7] = (uint8_t)(CAN_GetRXQueueCount() & 0xFF);

    return CAN_Send_Message(CAN_MOBO_HEARTBEAT_ID, heartbeat_data, 8, CAN_PRIORITY_HIGH);
}
*/

/*
HAL_StatusTypeDef CAN_SendStatistics(void) {
    uint8_t stats_data[8];

    stats_data[0] = (uint8_t)(canStats.rx_message_count & 0xFF);
    stats_data[1] = (uint8_t)((canStats.rx_message_count >> 8) & 0xFF);

    stats_data[2] = (uint8_t)(canStats.tx_success_count & 0xFF);
    stats_data[3] = (uint8_t)((canStats.tx_success_count >> 8) & 0xFF);

    stats_data[4] = (uint8_t)(canStats.tx_failure_count & 0xFF);
    stats_data[5] = (uint8_t)(canStats.tx_queue_full_count & 0xFF);
    stats_data[6] = (uint8_t)(canStats.rx_queue_full_count & 0xFF);
    stats_data[7] = (uint8_t)(canStats.bus_error_count & 0xFF);

    return CAN_Send_Message(CAN_MOBO_STATS_ID, stats_data, 8, CAN_PRIORITY_NORMAL);
}

*/

