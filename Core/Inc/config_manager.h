/**
  ******************************************************************************
  * @file    config_manager.h
  * @brief   Persistent configuration (flash-backed)
  *
  * Stores calibration constants and the last commanded relay mask in the very
  * last 2 KB flash page (0x0803F800), which is outside both Bank A and Bank B
  * linker regions and survives a firmware update.
  *
  * The relay mask is written debounced (max 1 write per change after a settle
  * period) by the power_manager to limit flash wear.
  ******************************************************************************
  */

#ifndef __CONFIG_MANAGER_H
#define __CONFIG_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "cmsis_os.h"
#include <stdint.h>
#include <stdbool.h>

/* Flash layout -------------------------------------------------------------*/
/* Last page of 256 KB flash, 2 KB. Outside Bank A (0x08008000-0x08021FFF)
 * and Bank B (0x08022000-0x0803BFFF). Free for config use. */
#define CONFIG_FLASH_ADDR     0x0803F800U
#define CONFIG_FLASH_PAGE     127U     /**< Page index for STM32L432KC (2 KB pages) */
#define CONFIG_MAGIC          0xC0DEC0FFEE5AA500ULL  /**< Arbitrary 64-bit signature */

/* Defaults applied when flash is blank or magic mismatch -------------------*/
#define CONFIG_DEFAULT_LV_OFFSET_MA    200
#define CONFIG_DEFAULT_HC_OFFSET_MA    200
/* Battery sense uses a resistor divider; mV = (raw * 3300 * NUM) / (4095 * DEN).
 * Default divider keeps the legacy "raw * 330 * 92 / 4095" magic until the
 * actual divider ratio is verified on hardware. */
#define CONFIG_DEFAULT_VDIV_NUM        92
#define CONFIG_DEFAULT_VDIV_DEN        10
/* RPI authority lapses after this period of RPI silence */
#define CONFIG_DEFAULT_RPI_TIMEOUT_MS  5000U

/**
 * @brief Persisted configuration record.
 *
 * Packed into a small struct so it fits in one flash double-word write
 * sequence. Add fields at the end only — old images remain readable as long
 * as the magic and crc match the size we expect.
 */
typedef struct __attribute__((packed)) {
    uint64_t magic;             /**< CONFIG_MAGIC */
    uint8_t  desired_relay_mask;/**< Last commanded relay mask (bit0=Pump..bit3=Rad) */
    uint8_t  reserved_a;
    int16_t  lv_current_offset_ma;
    int16_t  hc_current_offset_ma;
    uint16_t voltage_divider_num;
    uint16_t voltage_divider_den;
    uint32_t rpi_authority_timeout_ms;
    uint32_t crc32;             /**< CRC-32 of all preceding bytes (0 = unset) */
} Config_Record_t;

/* Public API ---------------------------------------------------------------*/

/**
 * @brief  Load config from flash, falling back to defaults if magic/crc fail.
 *         Always succeeds (returns HAL_OK) unless the mutex can't be created.
 */
HAL_StatusTypeDef Config_Init(void);

/** Get a snapshot of the in-RAM config record. Thread-safe. */
void Config_GetSnapshot(Config_Record_t *out);

/** Last commanded relay mask, restored from flash at boot. */
uint8_t Config_GetDesiredRelayMask(void);

/**
 * @brief  Persist a new desired relay mask. No-op if mask unchanged. Returns
 *         HAL_OK if flash write succeeds (or mask was already current).
 *         Caller (power_manager) is expected to debounce.
 */
HAL_StatusTypeDef Config_SaveDesiredRelayMask(uint8_t mask);

int16_t  Config_GetLvOffsetMa(void);
int16_t  Config_GetHcOffsetMa(void);
uint16_t Config_GetVoltageDividerNum(void);
uint16_t Config_GetVoltageDividerDen(void);
uint32_t Config_GetRpiTimeoutMs(void);

/**
 * @brief  Update one runtime parameter and persist.
 * @param  param_id: see CONFIG_PARAM_* in this header.
 * @param  value:    interpretation depends on param_id (see header).
 * @retval HAL_OK on success, HAL_ERROR on bad param_id or flash failure.
 */
HAL_StatusTypeDef Config_SetParam(uint8_t param_id, int32_t value);

#define CONFIG_PARAM_LV_OFFSET_MA      0x01  /**< int16_t mA */
#define CONFIG_PARAM_HC_OFFSET_MA      0x02  /**< int16_t mA */
#define CONFIG_PARAM_VDIV_NUM          0x03  /**< uint16_t */
#define CONFIG_PARAM_VDIV_DEN          0x04  /**< uint16_t (must be > 0) */
#define CONFIG_PARAM_RPI_TIMEOUT_MS    0x05  /**< uint32_t ms */

#ifdef __cplusplus
}
#endif

#endif /* __CONFIG_MANAGER_H */
