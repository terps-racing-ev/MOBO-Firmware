/**
  ******************************************************************************
  * @file    config_manager.c
  ******************************************************************************
  */

#include "config_manager.h"
#include "error_manager.h"
#include <string.h>
#include <stddef.h>
#include <limits.h>
#include <stdint.h>

extern CRC_HandleTypeDef hcrc;

static Config_Record_t g_cfg;
static osMutexId_t     g_mutex = NULL;

static const osMutexAttr_t g_mutex_attr = {
    .name = "ConfigMutex",
};

/* Private helpers ----------------------------------------------------------*/

/* CRC over all bytes of the record except the trailing crc32 field. */
static uint32_t Config_ComputeCRC(const Config_Record_t *rec)
{
    /* Use the HAL CRC peripheral (default poly, MPEG-2 style). Input is byte
     * stream up to but not including the trailing crc32. */
    const uint32_t bytes = (uint32_t)offsetof(Config_Record_t, crc32);
    /* HAL_CRC_Calculate expects 32-bit-word count when input format is words.
     * We configured CRC for byte input in main.c/MX_CRC_Init, so call
     * HAL_CRC_Calculate with the byte length. */
    return HAL_CRC_Calculate(&hcrc, (uint32_t *)(uintptr_t)rec, bytes);
}

static void Config_LoadDefaults(Config_Record_t *rec)
{
    memset(rec, 0, sizeof(*rec));
    rec->magic                    = CONFIG_MAGIC;
    rec->desired_relay_mask       = 0;
    rec->lv_current_offset_ma     = CONFIG_DEFAULT_LV_OFFSET_MA;
    rec->hc_current_offset_ma     = CONFIG_DEFAULT_HC_OFFSET_MA;
    rec->voltage_divider_num      = CONFIG_DEFAULT_VDIV_NUM;
    rec->voltage_divider_den      = CONFIG_DEFAULT_VDIV_DEN;
    rec->rpi_authority_timeout_ms = CONFIG_DEFAULT_RPI_TIMEOUT_MS;
}

/* Write the current g_cfg to flash. Caller must hold g_mutex.
 * Note: does NOT call ErrorMgr_SetError on failure to avoid lock-order
 * issues (error_manager may call into power_manager which calls config).
 * Caller should propagate non-OK status and report the error itself. */
static HAL_StatusTypeDef Config_Persist(void)
{
    HAL_StatusTypeDef status;
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t page_error = 0;

    g_cfg.crc32 = Config_ComputeCRC(&g_cfg);

    status = HAL_FLASH_Unlock();
    if (status != HAL_OK) {
        return status;
    }

    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.Banks     = FLASH_BANK_1;
    erase.Page      = CONFIG_FLASH_PAGE;
    erase.NbPages   = 1;

    status = HAL_FLASHEx_Erase(&erase, &page_error);
    if (status != HAL_OK) {
        HAL_FLASH_Lock();
        return status;
    }

    /* STM32L4 flash programming unit is 64-bit (double-word). Pad sizeof
     * upward to a multiple of 8 bytes; trailing bytes will read 0xFF. */
    const uint8_t *src = (const uint8_t *)&g_cfg;
    uint32_t addr = CONFIG_FLASH_ADDR;
    uint32_t remaining = sizeof(g_cfg);

    while (remaining > 0U) {
        uint64_t dw = 0xFFFFFFFFFFFFFFFFULL;
        uint32_t chunk = (remaining >= 8U) ? 8U : remaining;
        memcpy(&dw, src, chunk);

        status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, addr, dw);
        if (status != HAL_OK) {
            HAL_FLASH_Lock();
            return status;
        }

        src       += chunk;
        addr      += 8U;
        remaining -= chunk;
    }

    HAL_FLASH_Lock();
    return HAL_OK;
}

/* Public API ---------------------------------------------------------------*/

HAL_StatusTypeDef Config_Init(void)
{
    g_mutex = osMutexNew(&g_mutex_attr);
    if (g_mutex == NULL) {
        return HAL_ERROR;
    }

    /* Read raw record from flash. */
    Config_Record_t flash_rec;
    memcpy(&flash_rec, (const void *)CONFIG_FLASH_ADDR, sizeof(flash_rec));

    if (flash_rec.magic == CONFIG_MAGIC) {
        uint32_t expected = flash_rec.crc32;
        flash_rec.crc32 = 0;
        uint32_t actual = Config_ComputeCRC(&flash_rec);
        flash_rec.crc32 = expected;

        if (expected == actual) {
            g_cfg = flash_rec;
            return HAL_OK;
        }
    }

    /* Magic or CRC bad — apply defaults. Do NOT auto-persist; let the first
     * legitimate change carry the cost. */
    Config_LoadDefaults(&g_cfg);
    return HAL_OK;
}

void Config_GetSnapshot(Config_Record_t *out)
{
    if (out == NULL) return;
    if (osMutexAcquire(g_mutex, osWaitForever) == osOK) {
        *out = g_cfg;
        osMutexRelease(g_mutex);
    }
}

uint8_t Config_GetDesiredRelayMask(void)
{
    uint8_t v = 0;
    if (osMutexAcquire(g_mutex, osWaitForever) == osOK) {
        v = g_cfg.desired_relay_mask;
        osMutexRelease(g_mutex);
    }
    return v;
}

HAL_StatusTypeDef Config_SaveDesiredRelayMask(uint8_t mask)
{
    HAL_StatusTypeDef status = HAL_OK;
    bool need_write = false;

    if (osMutexAcquire(g_mutex, osWaitForever) == osOK) {
        if (g_cfg.desired_relay_mask != mask) {
            g_cfg.desired_relay_mask = mask;
            need_write = true;
        }
        if (need_write) {
            status = Config_Persist();
        }
        osMutexRelease(g_mutex);
    }
    if (status != HAL_OK) {
        ErrorMgr_SetError(ERROR_FLASH_FAULT);
    }
    return status;
}

int16_t Config_GetLvOffsetMa(void)
{
    int16_t v = CONFIG_DEFAULT_LV_OFFSET_MA;
    if (osMutexAcquire(g_mutex, osWaitForever) == osOK) {
        v = g_cfg.lv_current_offset_ma;
        osMutexRelease(g_mutex);
    }
    return v;
}

int16_t Config_GetHcOffsetMa(void)
{
    int16_t v = CONFIG_DEFAULT_HC_OFFSET_MA;
    if (osMutexAcquire(g_mutex, osWaitForever) == osOK) {
        v = g_cfg.hc_current_offset_ma;
        osMutexRelease(g_mutex);
    }
    return v;
}

uint16_t Config_GetVoltageDividerNum(void)
{
    uint16_t v = CONFIG_DEFAULT_VDIV_NUM;
    if (osMutexAcquire(g_mutex, osWaitForever) == osOK) {
        v = g_cfg.voltage_divider_num;
        osMutexRelease(g_mutex);
    }
    return v;
}

uint16_t Config_GetVoltageDividerDen(void)
{
    uint16_t v = CONFIG_DEFAULT_VDIV_DEN;
    if (osMutexAcquire(g_mutex, osWaitForever) == osOK) {
        v = g_cfg.voltage_divider_den;
        if (v == 0U) {
            v = CONFIG_DEFAULT_VDIV_DEN;
        }
        osMutexRelease(g_mutex);
    }
    return v;
}

uint32_t Config_GetRpiTimeoutMs(void)
{
    uint32_t v = CONFIG_DEFAULT_RPI_TIMEOUT_MS;
    if (osMutexAcquire(g_mutex, osWaitForever) == osOK) {
        v = g_cfg.rpi_authority_timeout_ms;
        osMutexRelease(g_mutex);
    }
    return v;
}

HAL_StatusTypeDef Config_SetParam(uint8_t param_id, int32_t value)
{
    HAL_StatusTypeDef status = HAL_ERROR;

    if (osMutexAcquire(g_mutex, osWaitForever) != osOK) {
        return HAL_ERROR;
    }

    bool changed = false;
    switch (param_id) {
    case CONFIG_PARAM_LV_OFFSET_MA:
        if (value >= INT16_MIN && value <= INT16_MAX) {
            if (g_cfg.lv_current_offset_ma != (int16_t)value) {
                g_cfg.lv_current_offset_ma = (int16_t)value;
                changed = true;
            }
            status = HAL_OK;
        }
        break;
    case CONFIG_PARAM_HC_OFFSET_MA:
        if (value >= INT16_MIN && value <= INT16_MAX) {
            if (g_cfg.hc_current_offset_ma != (int16_t)value) {
                g_cfg.hc_current_offset_ma = (int16_t)value;
                changed = true;
            }
            status = HAL_OK;
        }
        break;
    case CONFIG_PARAM_VDIV_NUM:
        if (value > 0 && value <= UINT16_MAX) {
            if (g_cfg.voltage_divider_num != (uint16_t)value) {
                g_cfg.voltage_divider_num = (uint16_t)value;
                changed = true;
            }
            status = HAL_OK;
        }
        break;
    case CONFIG_PARAM_VDIV_DEN:
        if (value > 0 && value <= UINT16_MAX) {
            if (g_cfg.voltage_divider_den != (uint16_t)value) {
                g_cfg.voltage_divider_den = (uint16_t)value;
                changed = true;
            }
            status = HAL_OK;
        }
        break;
    case CONFIG_PARAM_RPI_TIMEOUT_MS:
        if (value > 0) {
            if (g_cfg.rpi_authority_timeout_ms != (uint32_t)value) {
                g_cfg.rpi_authority_timeout_ms = (uint32_t)value;
                changed = true;
            }
            status = HAL_OK;
        }
        break;
    default:
        break;
    }

    if (status == HAL_OK && changed) {
        status = Config_Persist();
    }

    osMutexRelease(g_mutex);
    if (status != HAL_OK && changed) {
        ErrorMgr_SetError(ERROR_FLASH_FAULT);
    }
    return status;
}
