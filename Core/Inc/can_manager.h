/**
  ******************************************************************************
  * @file    can_manager.h
  * @brief   MOBO CAN bus manager: priority TX queue, RX queue, dispatch table,
  *          bus-off recovery, periodic telemetry encoding.
  *
  * Single owner of the CAN1 peripheral. All CAN traffic goes through this
  * module. RX messages are matched against a dispatch table; producers of
  * commands register their (match, handle) pair via CAN_RegisterDispatch().
  ******************************************************************************
  */

#ifndef __CAN_MANAGER_H
#define __CAN_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "cmsis_os.h"
#include "can_ids.h"
#include <stdint.h>
#include <stdbool.h>

/* Tunables -----------------------------------------------------------------*/
#define CAN_TX_QUEUE_SIZE          64U
#define CAN_RX_QUEUE_SIZE          32U
#define CAN_TX_TIMEOUT_MS          50U
#define CAN_MAX_RETRIES            3U
#define CAN_RECOVERY_COOLDOWN_MS   500U

#define CAN_PRIORITY_CRITICAL      0U  /**< Reset/safety responses */
#define CAN_PRIORITY_HIGH          1U  /**< Heartbeat, error frames */
#define CAN_PRIORITY_NORMAL        2U  /**< Telemetry */
#define CAN_PRIORITY_LOW           3U  /**< Stats */

/* Periodic TX cadence ------------------------------------------------------*/
#define CAN_HEARTBEAT_INTERVAL_MS         100U
#define CAN_ERRORS_INTERVAL_MS            500U
#define CAN_STATS_INTERVAL_MS            1000U
#define CAN_POWER_TELEM_INTERVAL_MS       200U
#define CAN_CURRENT_TELEM_INTERVAL_MS     100U
#define CAN_SAFETY_INTERVAL_MS            100U
#define CAN_RELAY_STATUS_INTERVAL_MS      100U

/* Message struct -----------------------------------------------------------*/
typedef struct {
    uint32_t id;
    uint8_t  data[8];
    uint8_t  length;
    uint8_t  priority;
    uint32_t timestamp;
} CAN_Message_t;

/* Dispatch ABI -------------------------------------------------------------*/
/* Producers of RX handlers expose a pair: a matcher returning true when the
 * frame is theirs, and a handler invoked with the frame. Order in the table
 * is the order in which they are tried. */
typedef bool (*CAN_MatchFunc_t)(const CAN_Message_t *msg);
typedef void (*CAN_HandleFunc_t)(const CAN_Message_t *msg);

typedef struct {
    CAN_MatchFunc_t  match;
    CAN_HandleFunc_t handle;
    const char      *name;
} CAN_DispatchEntry_t;

/* Statistics ---------------------------------------------------------------*/
typedef struct {
    uint32_t tx_success_count;
    uint32_t tx_error_count;
    uint32_t tx_queue_full_count;
    uint32_t rx_message_count;
    uint32_t rx_queue_full_count;
    uint32_t bus_off_count;
    uint32_t recovery_count;
} CAN_Statistics_t;

/* External handles ---------------------------------------------------------*/
extern CAN_HandleTypeDef hcan1;
extern osMessageQueueId_t CANTxQueueHandle;
extern osMessageQueueId_t CANRxQueueHandle;

/* Public API ---------------------------------------------------------------*/

/** Create queues, configure filter, activate notifications, start CAN1. */
HAL_StatusTypeDef CAN_Manager_Init(void);

/** Main task body. */
void CAN_ManagerTask(void *argument);

/** Queue a CAN frame for transmission. Non-blocking. */
HAL_StatusTypeDef CAN_SendMessage(uint32_t id, const uint8_t *data,
                                  uint8_t length, uint8_t priority);

void     CAN_GetStatistics(CAN_Statistics_t *out);
void     CAN_ResetStatistics(void);
uint32_t CAN_GetTxQueueCount(void);
uint32_t CAN_GetRxQueueCount(void);

/** True if a 29-bit ID is within the MOBO accept window. */
bool CAN_IsForMobo(uint32_t id);

#ifdef __cplusplus
}
#endif

#endif /* __CAN_MANAGER_H */
