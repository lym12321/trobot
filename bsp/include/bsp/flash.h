//
// Created by fish on 2026/1/6.
//

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bsp/def.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 初始化参数区并尝试初始化日志区；返回参数区状态。任务使用前调用。
 */
bsp_status_t bsp_flash_init(void);

/**
 * 获取参数区初始化状态；不受日志区初始化失败影响。
 */
bsp_status_t bsp_flash_status(void);

/**
 * 获取日志区初始化状态。
 */
bsp_status_t bsp_flash_log_status(void);
/* Maintenance boot only, before normal Flash initialization. Exposes the frozen
 * logical log area (including erased space at its tail) until reset. */
bsp_status_t bsp_flash_log_open_readonly(size_t *size);
/* Initialization only. A validated record can include erased words after the
 * detected tail. Retain those bytes in the current sector before appending. */
bool bsp_flash_log_confirm_end(size_t used, size_t extra);

void bsp_flash_read(const char *s, void *buf, size_t len);
uint8_t bsp_flash_write(const char *s, void *buf, size_t len);

/**
 * 复制至多 len 字节并报告完整保存长度。键不存在时返回 BSP_STATUS_NOT_FOUND。
 */
bsp_status_t bsp_flash_read_ex(
    const char *s, void *buf, size_t len, size_t *saved_len
);

/**
 * 将字段写入 flash 并返回完整状态
 */
bsp_status_t bsp_flash_write_ex(const char *s, const void *buf, size_t len);

bsp_status_t bsp_flash_log_read(size_t index, void *buf, size_t len);
/* 在一次加锁期间复制最近的至多 capacity 字节。空日志成功返回 *size = 0。 */
bsp_status_t bsp_flash_log_tail(void *buf, size_t capacity, size_t *size);
bsp_status_t bsp_flash_log_write(const void *buf, size_t len);
bsp_status_t bsp_flash_log_clean(void);
size_t bsp_flash_log_used_size(void);
size_t bsp_flash_log_total_size(void);

#ifdef __cplusplus
}
#endif
