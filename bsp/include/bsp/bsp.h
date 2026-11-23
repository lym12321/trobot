//
// Created by fish on 2025/9/24.
//

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "bsp/usb.h"

typedef enum {
    BSP_HW_LED    = 1U << 0,
    BSP_HW_CAN_1  = 1U << 1,
    BSP_HW_CAN_2  = 1U << 2,
    BSP_HW_CAN_3  = 1U << 3,
    BSP_HW_IMU    = 1U << 4,
    BSP_HW_ADC    = 1U << 5,
    BSP_HW_FLASH  = 1U << 6,
    BSP_HW_BUZZER = 1U << 7,
    BSP_HW_USB    = 1U << 9,
} bsp_hw_e;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 初始化板级硬件，usb 指向本次启动的 USB 配置。
 */
uint32_t bsp_hw_init(const bsp_usb_config_t *usb);
bool bsp_user_key_pressed(void);

/**
 * 喂狗
 */
void bsp_iwdg_refresh();

#ifdef __cplusplus
}
#endif
