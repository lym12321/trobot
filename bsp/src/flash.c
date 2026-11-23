#include "bsp/flash.h"

#include <stdint.h>

#include "FreeRTOS.h"
#include "semphr.h"

#include "bsp/flash_internal.h"
#include "bsp/sys.h"
#include "easyflash.h"

EfErrCode ef_port_init(ef_env const **default_env, size_t *default_env_size);
EfErrCode ef_env_init(ef_env const *default_env, size_t default_env_size);
EfErrCode ef_log_init(void);

static StaticSemaphore_t flash_mutex_buffer;
static SemaphoreHandle_t flash_mutex;
bool bsp_flash_initializing;
static volatile bsp_status_t flash_status = BSP_STATUS_OFFLINE;
static volatile bsp_status_t log_status = BSP_STATUS_OFFLINE;
static bool log_readonly;

void bsp_flash_internal_lock(void) {
    (void) xSemaphoreTakeRecursive(flash_mutex, portMAX_DELAY);
}

void bsp_flash_internal_unlock(void) {
    (void) xSemaphoreGiveRecursive(flash_mutex);
}

bsp_status_t bsp_flash_init(void) {
    if (bsp_sys_in_isr() || log_readonly) return BSP_STATUS_ERROR;
    if (flash_status == BSP_STATUS_OK && log_status == BSP_STATUS_OK) return BSP_STATUS_OK;

    if (flash_mutex == NULL) {
        flash_mutex = xSemaphoreCreateRecursiveMutexStatic(&flash_mutex_buffer);
        BSP_ASSERT(flash_mutex != NULL);
    }

    bsp_flash_internal_lock();
    bsp_flash_initializing = true;
    if (flash_status != BSP_STATUS_OK) {
        ef_env const *default_env;
        size_t default_env_size;
        flash_status = ef_port_init(&default_env, &default_env_size) == EF_NO_ERR &&
                       ef_env_init(default_env, default_env_size) == EF_NO_ERR
            ? BSP_STATUS_OK : BSP_STATUS_ERROR;
    }
    if (flash_status == BSP_STATUS_OK && log_status != BSP_STATUS_OK) {
        log_status = ef_log_init() == EF_NO_ERR ? BSP_STATUS_OK : BSP_STATUS_ERROR;
    }
    bsp_flash_initializing = false;
    bsp_flash_internal_unlock();
    return flash_status;
}

bsp_status_t bsp_flash_status(void) {
    return flash_status;
}

bsp_status_t bsp_flash_log_status(void) {
    return log_status;
}

bsp_status_t bsp_flash_log_open_readonly(size_t *size) {
    if (size == NULL || bsp_sys_in_isr() || flash_mutex != NULL) return BSP_STATUS_ERROR;
    *size = 0;
    log_readonly = true;
    flash_mutex = xSemaphoreCreateRecursiveMutexStatic(&flash_mutex_buffer);
    BSP_ASSERT(flash_mutex != NULL);
    ef_env const *defaults;
    size_t count;
    bsp_flash_initializing = true;
    if (ef_port_init(&defaults, &count) == EF_NO_ERR && ef_log_open(true) == EF_NO_ERR) {
        *size = ef_log_get_used_size();
        log_status = BSP_STATUS_OK;
    } else {
        log_status = BSP_STATUS_ERROR;
    }
    bsp_flash_initializing = false;
    return log_status;
}

bsp_status_t bsp_flash_read_ex(const char *s, void *buf, size_t len, size_t *saved_len) {
    if (bsp_sys_in_isr() || flash_status != BSP_STATUS_OK || s == NULL ||
        buf == NULL || len == 0 || saved_len == NULL) {
        return BSP_STATUS_ERROR;
    }

    const EfErrCode result = ef_get_env_blob_status(s, buf, len, saved_len);
    if (result == EF_ENV_NAME_ERR) return BSP_STATUS_NOT_FOUND;
    return result == EF_NO_ERR ? BSP_STATUS_OK : BSP_STATUS_ERROR;
}

bsp_status_t bsp_flash_write_ex(const char *s, const void *buf, size_t len) {
    if (bsp_sys_in_isr() || flash_status != BSP_STATUS_OK || s == NULL ||
        buf == NULL || len == 0) {
        return BSP_STATUS_ERROR;
    }

    bsp_flash_internal_lock();
    const EfErrCode result = ef_set_env_blob(s, buf, len);
    if (result == EF_ENV_INIT_FAILED) flash_status = BSP_STATUS_ERROR;
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

bool bsp_flash_log_confirm_end(size_t used, size_t extra) {
    if (bsp_sys_in_isr() || log_status != BSP_STATUS_OK || log_readonly) return false;
    bsp_flash_internal_lock();
    const bool ok = ef_log_confirm_end(used, extra);
    bsp_flash_internal_unlock();
    return ok;
}

bsp_status_t bsp_flash_log_read(size_t index, void *buf, size_t len) {
    if (bsp_sys_in_isr() || log_status != BSP_STATUS_OK || buf == NULL || len == 0) {
        return BSP_STATUS_ERROR;
    }

    bsp_flash_internal_lock();
    const EfErrCode result = ef_log_read(index, buf, len);
    bsp_flash_internal_unlock();
    return result == EF_NO_ERR ? BSP_STATUS_OK : BSP_STATUS_ERROR;
}

bsp_status_t bsp_flash_log_tail(void *buf, size_t capacity, size_t *size) {
    if (size == NULL) return BSP_STATUS_ERROR;
    *size = 0;
    if (bsp_sys_in_isr() || log_status != BSP_STATUS_OK || buf == NULL || capacity == 0) {
        return BSP_STATUS_ERROR;
    }

    bsp_flash_internal_lock();
    const size_t used = ef_log_get_used_size();
    const size_t len = used < capacity ? used : capacity;
    const EfErrCode result = ef_log_read(used - len, buf, len);
    if (result == EF_NO_ERR) *size = len;
    bsp_flash_internal_unlock();
    return result == EF_NO_ERR ? BSP_STATUS_OK : BSP_STATUS_ERROR;
}

bsp_status_t bsp_flash_log_write(const void *buf, size_t len) {
    if (bsp_sys_in_isr() || log_readonly || log_status != BSP_STATUS_OK || buf == NULL ||
        len == 0 || len % sizeof(uint32_t) != 0 ||
        (uintptr_t) buf % _Alignof(uint32_t) != 0) {
        return BSP_STATUS_ERROR;
    }

    bsp_flash_internal_lock();
    const EfErrCode result = ef_log_write(buf, len);
    if (result != EF_NO_ERR) log_status = BSP_STATUS_ERROR;
    bsp_flash_internal_unlock();
    return result == EF_NO_ERR ? BSP_STATUS_OK : BSP_STATUS_ERROR;
}

bsp_status_t bsp_flash_log_clean(void) {
    if (bsp_sys_in_isr() || log_readonly || log_status != BSP_STATUS_OK) return BSP_STATUS_ERROR;

    bsp_flash_internal_lock();
    const EfErrCode result = ef_log_clean();
    if (result != EF_NO_ERR) log_status = BSP_STATUS_ERROR;
    bsp_flash_internal_unlock();
    return result == EF_NO_ERR ? BSP_STATUS_OK : BSP_STATUS_ERROR;
}

size_t bsp_flash_log_used_size(void) {
    if (bsp_sys_in_isr() || log_status != BSP_STATUS_OK) return 0;

    bsp_flash_internal_lock();
    const size_t size = ef_log_get_used_size();
    bsp_flash_internal_unlock();
    return size;
}

size_t bsp_flash_log_total_size(void) {
    if (bsp_sys_in_isr() || log_status != BSP_STATUS_OK) return 0;

    bsp_flash_internal_lock();
    const size_t size = ef_log_get_total_size();
    bsp_flash_internal_unlock();
    return size;
}
