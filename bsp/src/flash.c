#include "bsp/flash.h"

#include <stdint.h>

#include "FreeRTOS.h"
#include "semphr.h"

#include "bsp/flash_internal.h"
#include "bsp/sys.h"
#include "easyflash.h"

static StaticSemaphore_t flash_mutex_buffer;
static SemaphoreHandle_t flash_mutex;
static volatile bsp_status_t flash_status = BSP_STATUS_OFFLINE;

void bsp_flash_internal_lock(void) {
    (void) xSemaphoreTakeRecursive(flash_mutex, portMAX_DELAY);
}

void bsp_flash_internal_unlock(void) {
    (void) xSemaphoreGiveRecursive(flash_mutex);
}

bsp_status_t bsp_flash_init(void) {
    if (bsp_sys_in_isr()) return BSP_STATUS_ERROR;
    if (flash_status == BSP_STATUS_OK) return BSP_STATUS_OK;

    if (flash_mutex == NULL) {
        flash_mutex = xSemaphoreCreateRecursiveMutexStatic(&flash_mutex_buffer);
    }

    flash_status = easyflash_init() == EF_NO_ERR ? BSP_STATUS_OK : BSP_STATUS_ERROR;
    return flash_status;
}

bsp_status_t bsp_flash_status(void) {
    return flash_status;
}

bsp_status_t bsp_flash_read_ex(const char *s, void *buf, size_t len, size_t *saved_len) {
    if (bsp_sys_in_isr() || flash_status != BSP_STATUS_OK || s == NULL ||
        buf == NULL || len == 0 || saved_len == NULL) {
        return BSP_STATUS_ERROR;
    }

    bsp_flash_internal_lock();
    *saved_len = 0;
    const size_t read_len = ef_get_env_blob(s, buf, len, saved_len);
    bsp_flash_internal_unlock();
    return read_len > 0 ? BSP_STATUS_OK : BSP_STATUS_ERROR;
}

bsp_status_t bsp_flash_write_ex(const char *s, const void *buf, size_t len) {
    if (bsp_sys_in_isr() || flash_status != BSP_STATUS_OK || s == NULL ||
        buf == NULL || len == 0) {
        return BSP_STATUS_ERROR;
    }

    bsp_flash_internal_lock();
    const EfErrCode result = ef_set_env_blob(s, buf, len);
    bsp_flash_internal_unlock();
    return result == EF_NO_ERR ? BSP_STATUS_OK : BSP_STATUS_ERROR;
}

void bsp_flash_read(const char *s, void *buf, size_t len) {
    size_t saved_len;
    (void) bsp_flash_read_ex(s, buf, len, &saved_len);
}

uint8_t bsp_flash_write(const char *s, void *buf, size_t len) {
    return bsp_flash_write_ex(s, buf, len) == BSP_STATUS_OK;
}

bsp_status_t bsp_flash_log_read(size_t index, void *buf, size_t len) {
    if (bsp_sys_in_isr() || flash_status != BSP_STATUS_OK || buf == NULL ||
        len == 0 || len % sizeof(uint32_t) != 0 ||
        (uintptr_t) buf % _Alignof(uint32_t) != 0) {
        return BSP_STATUS_ERROR;
    }

    bsp_flash_internal_lock();
    const EfErrCode result = ef_log_read(index, buf, len);
    bsp_flash_internal_unlock();
    return result == EF_NO_ERR ? BSP_STATUS_OK : BSP_STATUS_ERROR;
}

bsp_status_t bsp_flash_log_write(const void *buf, size_t len) {
    if (bsp_sys_in_isr() || flash_status != BSP_STATUS_OK || buf == NULL ||
        len == 0 || len % sizeof(uint32_t) != 0 ||
        (uintptr_t) buf % _Alignof(uint32_t) != 0) {
        return BSP_STATUS_ERROR;
    }

    bsp_flash_internal_lock();
    const EfErrCode result = ef_log_write(buf, len);
    bsp_flash_internal_unlock();
    return result == EF_NO_ERR ? BSP_STATUS_OK : BSP_STATUS_ERROR;
}

bsp_status_t bsp_flash_log_clean(void) {
    if (bsp_sys_in_isr() || flash_status != BSP_STATUS_OK) return BSP_STATUS_ERROR;

    bsp_flash_internal_lock();
    const EfErrCode result = ef_log_clean();
    bsp_flash_internal_unlock();
    return result == EF_NO_ERR ? BSP_STATUS_OK : BSP_STATUS_ERROR;
}

size_t bsp_flash_log_used_size(void) {
    if (bsp_sys_in_isr() || flash_status != BSP_STATUS_OK) return 0;

    bsp_flash_internal_lock();
    const size_t size = ef_log_get_used_size();
    bsp_flash_internal_unlock();
    return size;
}

size_t bsp_flash_log_total_size(void) {
    if (bsp_sys_in_isr() || flash_status != BSP_STATUS_OK) return 0;

    bsp_flash_internal_lock();
    const size_t size = ef_log_get_total_size();
    bsp_flash_internal_unlock();
    return size;
}

void bsp_flash_print_status(void) {
    if (bsp_sys_in_isr() || flash_status != BSP_STATUS_OK) return;

    bsp_flash_internal_lock();
    ef_print_env();
    bsp_flash_internal_unlock();
}
