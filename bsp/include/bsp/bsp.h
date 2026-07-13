//
// Created by fish on 2025/9/24.
//

#pragma once

#include <stdint.h>

typedef enum {
    BSP_HW_LED    = 1U << 0,
    BSP_HW_CAN_1  = 1U << 1,
    BSP_HW_CAN_2  = 1U << 2,
    BSP_HW_CAN_3  = 1U << 3,
    BSP_HW_IMU    = 1U << 4,
    BSP_HW_ADC    = 1U << 5,
    BSP_HW_FLASH  = 1U << 6,
    BSP_HW_BUZZER = 1U << 7,
} bsp_hw_e;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * bsp 硬件初始化
 */
uint32_t bsp_hw_init();

/**
 * 喂狗
 */
void bsp_iwdg_refresh();

#ifdef __cplusplus
}
#endif
