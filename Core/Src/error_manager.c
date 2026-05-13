/**
  ******************************************************************************
  * @file    error_manager.c
  ******************************************************************************
  */

#include "error_manager.h"
#include <string.h>

static Error_Status_t g_status = {0};
static osMutexId_t    g_mutex  = NULL;

static const osMutexAttr_t g_mutex_attr = {
    .name = "ErrorMutex",
};

HAL_StatusTypeDef ErrorMgr_Init(void)
{
    memset(&g_status, 0, sizeof(g_status));
    g_mutex = osMutexNew(&g_mutex_attr);
    return (g_mutex != NULL) ? HAL_OK : HAL_ERROR;
}

static uint8_t popcount32(uint32_t x)
{
    uint8_t n = 0;
    while (x) { n += (uint8_t)(x & 1U); x >>= 1; }
    return n;
}

void ErrorMgr_SetError(uint32_t flags)
{
    if (osMutexAcquire(g_mutex, osWaitForever) != osOK) {
        return;
    }

    uint32_t newly_set = flags & ~g_status.error_flags;
    if (newly_set != 0U) {
        g_status.fault_count += popcount32(newly_set);
    }
    g_status.error_flags |= flags;

    osMutexRelease(g_mutex);
}

void ErrorMgr_ClearError(uint32_t flags)
{
    if (osMutexAcquire(g_mutex, osWaitForever) == osOK) {
        g_status.error_flags &= ~flags;
        osMutexRelease(g_mutex);
    }
}

void ErrorMgr_SetWarning(uint32_t flags)
{
    if (osMutexAcquire(g_mutex, osWaitForever) == osOK) {
        g_status.warning_flags |= flags;
        osMutexRelease(g_mutex);
    }
}

void ErrorMgr_ClearWarning(uint32_t flags)
{
    if (osMutexAcquire(g_mutex, osWaitForever) == osOK) {
        g_status.warning_flags &= ~flags;
        osMutexRelease(g_mutex);
    }
}

uint32_t ErrorMgr_GetErrors(void)
{
    uint32_t v = 0;
    if (osMutexAcquire(g_mutex, osWaitForever) == osOK) {
        v = g_status.error_flags;
        osMutexRelease(g_mutex);
    }
    return v;
}

uint32_t ErrorMgr_GetWarnings(void)
{
    uint32_t v = 0;
    if (osMutexAcquire(g_mutex, osWaitForever) == osOK) {
        v = g_status.warning_flags;
        osMutexRelease(g_mutex);
    }
    return v;
}

bool ErrorMgr_HasErrors(void)
{
    return (ErrorMgr_GetErrors() != 0U);
}

void ErrorMgr_GetStatus(Error_Status_t *out)
{
    if (out == NULL) return;
    if (osMutexAcquire(g_mutex, osWaitForever) == osOK) {
        *out = g_status;
        osMutexRelease(g_mutex);
    }
}

void ErrorMgr_TickUptime(void)
{
    if (osMutexAcquire(g_mutex, osWaitForever) == osOK) {
        g_status.uptime_seconds++;
        osMutexRelease(g_mutex);
    }
}
