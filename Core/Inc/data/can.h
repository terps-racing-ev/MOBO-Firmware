#ifndef DATA_CAN_H
#define DATA_CAN_H

#include "stm32l4xx_hal.h"
#include "can_id.h"
#include <stdint.h>

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

HAL_StatusTypeDef CAN_Send_Message(uint32_t id, uint8_t *data, uint8_t length, uint8_t priority);

#endif /* DATA_CAN_H */
