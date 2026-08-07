//
// Created by fish on 2026/1/6.
//

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "bsp/def.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 初始化板载 flash
 */
bsp_status_t bsp_flash_init(void);

/**
 * 获取板载 flash 初始化状态
 */
bsp_status_t bsp_flash_status(void);

/**
 * 从 flash 中读取指定字段的数据到指定地址
 * @param s 字段名
 * @param buf 内存指针
 * @param len 内存长度
 */
void bsp_flash_read(const char *s, void *buf, size_t len);

/**
 * 将指定地址的数据写入 flash 中指定字段
 * @param s 字段名
 * @param buf 内存指针
 * @param len 内存长度
 */
uint8_t bsp_flash_write(const char *s, void *buf, size_t len);

/**
 * 从 flash 读取字段并返回完整状态与保存长度
 */
bsp_status_t bsp_flash_read_ex(
    const char *s, void *buf, size_t len, size_t *saved_len
);

/**
 * 将字段写入 flash 并返回完整状态
 */
bsp_status_t bsp_flash_write_ex(const char *s, const void *buf, size_t len);

bsp_status_t bsp_flash_log_read(size_t index, void *buf, size_t len);
bsp_status_t bsp_flash_log_write(const void *buf, size_t len);
bsp_status_t bsp_flash_log_clean(void);
size_t bsp_flash_log_used_size(void);
size_t bsp_flash_log_total_size(void);

void bsp_flash_print_status(void);

#ifdef __cplusplus
}
#endif
