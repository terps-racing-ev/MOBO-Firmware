#ifndef __CAN_H
#define __CAN_H

#include "cmsis_os.h"
#include "stm32l4xx_hal.h"
#include "can_id.h"
#include <stdint.h>
#include <stdbool.h>

#define CAN_TX_QUEUE_SIZE 64
#define CAN_RX_QUEUE_SIZE 32
#define CAN_TX_TIMEOUT_MS 100
#define CAN_MAX_RETRIES 3
#define CAN_HEARTBEAT_INTERVAL_MS 1000

#define CAN_PRIORITY_CRITICAL 0
#define CAN_PRIORITY_HIGH 1
#define CAN_PRIORITY_NORMAL 2
#define CAN_PRIORITY_LOW 3

typedef struct {
    uint32_t id;          // CAN message ID
    uint8_t data[8];      // CAN message data (up to 8 bytes)
    uint8_t length;       // Length of the data (0-8)
    uint8_t priority;     // Message priority (0-3)
    uint32_t timestamp;    // Timestamp for message timing
} CAN_Message_t;

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



HAL_StatusTypeDef CAN_Init(void);
void CAN_Task(void *argument);
HAL_StatusTypeDef CAN_Send_Message(uint32_t id, uint8_t *data, uint8_t length, uint8_t priority);
void CAN_Get_Stats(CAN_Stats_t *stats);
void CAN_Reset_Stats(CAN_Stats_t *stats);
uint32_t CAN_GetTXQueueCount(void);
uint32_t CAN_GetRXQueueCount(void);
uint32_t CAN_Flush_TX_Queue(void);


HAL_StatusTypeDef CAN_MOBO_Summary(void);
HAL_StatusTypeDef CAN_Power_INFO(void);
HAL_StatusTypeDef CAN_MOBO_LC_Summary(void);
HAL_StatusTypeDef CAN_MOBO_HC_Summary(void);

//HAL_StatusTypeDef CAN_SendHeartbeat(void);
//HAL_StatusTypeDef CAN_SendStatistics(void);

#endif /* __CAN_H */
