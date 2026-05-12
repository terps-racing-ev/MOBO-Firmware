#ifndef CAN_MANAGER_H
#define CAN_MANAGER_H

#include "cmsis_os.h"
#include "data/can.h"
#include <stdint.h>

#define CAN_TX_QUEUE_SIZE 64
#define CAN_RX_QUEUE_SIZE 32
#define CAN_TX_TIMEOUT_MS 100
#define CAN_MAX_RETRIES 3

typedef struct {
    uint32_t tx_success_count;   // Number of successful transmissions
    uint32_t tx_failure_count;   // Number of failed transmissions
    uint32_t tx_queue_full_count;  // Number of times the TX queue was full
    uint32_t rx_message_count;    // Number of received messages
    uint32_t rx_queue_full_count;  // Number of times the RX queue was full
    uint32_t bus_error_count;     // Number of bus errors
} CAN_Stats_t;

extern osMessageQueueId_t canTxQueueHandle;
extern osMessageQueueId_t canRxQueueHandle;
extern CAN_HandleTypeDef hcan1;
extern CAN_Stats_t canStats;

HAL_StatusTypeDef CAN_Init(void);
void CAN_ManagerTask(void *argument);
void CAN_Get_Stats(CAN_Stats_t *stats);
void CAN_Reset_Stats(CAN_Stats_t *stats);
uint32_t CAN_GetTXQueueCount(void);
uint32_t CAN_GetRXQueueCount(void);
uint32_t CAN_Flush_TX_Queue(void);

typedef void (*CAN_RxHandler_t)(CAN_Message_t *message);

#include "drivers/debug.h"
#include "drivers/hc_control.h"

#ifdef CAN_MANAGER_IMPLEMENTATION
const CAN_RxHandler_t canRxHandlers[] = {
    Debug_HandleCanReset,
    HC_Control_HandleRpiCommand,
    HC_Control_HandleVcuCommand
};
const uint32_t canRxHandlerCount = (uint32_t)(sizeof(canRxHandlers) / sizeof(canRxHandlers[0]));
#else
extern const CAN_RxHandler_t canRxHandlers[];
extern const uint32_t canRxHandlerCount;
#endif

#endif /* CAN_MANAGER_H */
