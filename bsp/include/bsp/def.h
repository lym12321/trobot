//
// Created by fish on 2025/9/19.
//

#pragma once

#include "main.h"
#include "stm32h7xx_hal.h"
#include "FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

void bsp_assert_failed(const char *expr, const char *file, int line);

#define BSP_ASSERT(expr) \
    do { if (!(expr)) bsp_assert_failed(#expr, __FILE__, __LINE__); } while (0)

#define BSP_ASSERT_MSG(expr, msg) \
    do { if (!(expr)) bsp_assert_failed(#expr, __FILE__, __LINE__); } while (0)

#define _ram_d1 __attribute__((section(".ram_d1")))

typedef struct {
    GPIO_TypeDef *port;
    uint32_t pin;
} bsp_io_t;

typedef TIM_HandleTypeDef bsp_timer_handle_t;

typedef UART_HandleTypeDef bsp_uart_handle_t;

typedef FDCAN_HandleTypeDef bsp_can_handle_t;

#ifdef __cplusplus
}
#endif