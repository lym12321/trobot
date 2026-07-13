//
// Created by fish on 2025/9/24.
//

#pragma once

#include "bsp/def.h"

#define BSP_CAN_DEVICE_COUNT 3
#define BSP_CAN_FILTER_LIMIT_STD 24
#define BSP_CAN_BUFFER_SIZE 64

typedef enum {
    E_CAN_1,
    E_CAN_2,
    E_CAN_3
} bsp_can_e;

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*bsp_can_callback_t)(bsp_can_e device, uint32_t id, const uint8_t *data, size_t len);

/**
 * 初始化指定 can 外设
 * @param device can 外设枚举类
 */
bsp_status_t bsp_can_init(bsp_can_e device);

/**
 * 注册 can 接收回调
 * @param device can 外设枚举类
 * @param id 要注册的 can id (0 <= id <= 0x7ff)
 * @param func 回调函数
 */
bsp_status_t bsp_can_set_callback(bsp_can_e device, uint32_t id, bsp_can_callback_t func);

/**
 * 发送 can 包
 * @param device can 外设枚举类
 * @param id 要发送的 can id (0 <= id <= 0x7ff)
 * @param data 要发送的数据指针，必须保证在发送完成前有效
 * @param len 要发送的数据长度，若 > 8 则使用 canfd 发送
 */
bsp_status_t bsp_can_send(bsp_can_e device, uint32_t id, const uint8_t *data, uint8_t len);

typedef struct {
    uint32_t tx_error_count;
    uint32_t rx_error_count;
    uint32_t bus_off_count;
    bsp_status_t last_status;
} bsp_can_stats_t;

bsp_can_stats_t bsp_can_stats(bsp_can_e device);

#ifdef __cplusplus
}
#endif
