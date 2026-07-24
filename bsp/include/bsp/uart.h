//
// Created by fish on 2025/9/19.
//

#pragma once

#include "bsp/def.h"

#define BSP_UART_DEVICE_COUNT 6
#define BSP_UART_BUFFER_SIZE 512

typedef enum {
    E_UART_1,
    E_UART_2,
    E_UART_3,
    E_UART_5,
    E_UART_7,
    E_UART_10
} bsp_uart_e;

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*bsp_uart_callback_t) (bsp_uart_e device, const uint8_t *data, size_t len);

/**
 * 串口阻塞发送
 * @param device 串口外设枚举类
 * @param data 待发送数据指针
 * @param len 待发送数据长度
 */
bsp_status_t bsp_uart_send(bsp_uart_e device, const uint8_t *data, uint32_t len);

/**
 * 串口异步发送
 * @param device 串口外设枚举类
 * @param data 待发送数据指针
 * @param len 待发送数据长度
 */
bsp_status_t bsp_uart_send_async(bsp_uart_e device, const uint8_t *data, uint32_t len);

/**
 * 串口阻塞打印 (格式化字符串和额外参数用法同标准 printf)
 * @param device 串口外设枚举类
 * @param fmt 格式化字符串
 * @param ... 额外参数
 */
bsp_status_t bsp_uart_printf(bsp_uart_e device, const char *fmt, ...);

/**
 * 串口异步打印 (格式化字符串和额外参数用法同标准 printf)
 * @param device 串口外设枚举类
 * @param fmt 格式化字符串
 * @param ... 额外参数
 */
bsp_status_t bsp_uart_printf_async(bsp_uart_e device, const char *fmt, ...);

/**
 * 设置串口接收回调
 * @param device 串口外设枚举类
 * @param func 回调函数
 */
bsp_status_t bsp_uart_set_callback(bsp_uart_e device, bsp_uart_callback_t func);

/**
 * 设置串口波特率, 尽量只在初始化时调用
 * @param device 串口外设枚举类
 * @param baudrate 波特率
 */
bsp_status_t bsp_uart_set_baudrate(bsp_uart_e device, uint32_t baudrate);

typedef struct {
    uint32_t tx_error_count;
    uint32_t rx_error_count;
    uint32_t tx_drop_count;
    bsp_status_t last_status;
} bsp_uart_stats_t;

bsp_uart_stats_t bsp_uart_stats(bsp_uart_e device);

#ifdef __cplusplus
}
#endif
