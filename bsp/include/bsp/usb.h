//
// Created by fish on 2026/1/6.
//

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bsp/def.h"

#define BSP_USB_CDC_BUFFER_SIZE       512
#define BSP_USB_WINUSB_REQUEST_SIZE   64
#define BSP_USB_WINUSB_RESPONSE_SIZE 16384

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*bsp_usb_cdc_callback_t)(const uint8_t *data, size_t len);
typedef void (*bsp_usb_winusb_callback_t)(const uint8_t *data, size_t len);

/**
 * 初始化 usb
 */
bsp_status_t bsp_usb_init(void);

/**
 * 查询 USB 是否已完成枚举配置
 */
bool bsp_usb_is_configured(void);

/**
 * 查询 WinUSB bulk IN 是否正在发送
 */
bool bsp_usb_winusb_tx_busy(void);

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

/**
 * 设置 usb cdc 接收回调，回调在 USB 中断中执行
 * @param callback 接收回调，数据仅在回调执行期间有效
 */
bsp_status_t bsp_usb_cdc_set_callback(bsp_usb_cdc_callback_t callback);

/**
 * 使用 WinUSB bulk IN 发送一条响应
 * @param data 发送缓冲区，传输完成前必须保持有效且内容不变
 * @param len 待发送数据长度
 */
bsp_status_t bsp_usb_winusb_send(const uint8_t *data, uint16_t len);

/**
 * 设置 WinUSB bulk OUT 接收回调，回调在 USB 中断中执行
 * @param callback 接收回调，数据仅在回调执行期间有效
 */
bsp_status_t bsp_usb_winusb_set_callback(bsp_usb_winusb_callback_t callback);

#ifdef __cplusplus
}
#endif
