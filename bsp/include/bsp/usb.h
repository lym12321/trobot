//
// Created by fish on 2026/1/6.
//

#pragma once

#include <stdint.h>
#include "bsp/def.h"

#define BSP_USB_CDC_BUFFER_SIZE 512

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 初始化 usb
 */
void bsp_usb_init();

/**
 * 使用 usb cdc 发送
 * @param data 待发送数据指针
 * @param len 待发送数据长度
 */
bsp_status_t bsp_usb_cdc_send(const uint8_t *data, uint16_t len);

/**
 * 使用 usb cdc 打印
 * @param fmt 格式化字符串
 * @param ... 额外参数
 */
bsp_status_t bsp_usb_cdc_printf(const char *fmt, ...);

#ifdef __cplusplus
}
#endif
