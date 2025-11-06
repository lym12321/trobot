//
// Created by fish on 2025/9/19.
//

#include "bsp/uart.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "usart.h"

static bsp_uart_handle_t *handle[BSP_UART_DEVICE_COUNT] = {
    [E_UART_1] = &huart1,
    [E_UART_2] = &huart2,
    [E_UART_3] = &huart3,
    [E_UART_5] = &huart5,
    [E_UART_7] = &huart7,
    [E_UART_10] = &huart10
};

static uint8_t lock[BSP_UART_DEVICE_COUNT];
static bsp_uart_callback_t callback[BSP_UART_DEVICE_COUNT];
_ram_d1 static uint8_t tx_buffer[BSP_UART_DEVICE_COUNT][BSP_UART_BUFFER_SIZE];
_ram_d1 static uint8_t rx_buffer[BSP_UART_DEVICE_COUNT][BSP_UART_BUFFER_SIZE];

void bsp_uart_send(bsp_uart_e device, const uint8_t *data, const uint32_t len) {
    BSP_ASSERT(0 <= device && device < BSP_UART_DEVICE_COUNT && data != NULL && len > 0);
    HAL_UART_Transmit(handle[device], data, len, HAL_MAX_DELAY);
}

void bsp_uart_send_async(bsp_uart_e device, const uint8_t *data, const uint32_t len) {
    BSP_ASSERT(0 <= device && device < BSP_UART_DEVICE_COUNT && data != NULL && len > 0);

    // while (lock[device]) __NOP();            // 等待空闲，不会丢包，但一定程度上阻塞
    if (lock[device]) return;                   // 直接丢弃，会丢包，但不阻塞任务
    lock[device] = 1;

    if (data != tx_buffer[device]) {
        memcpy(tx_buffer[device], data, len);
        data = tx_buffer[device];
    }
    if (handle[device]->hdmatx) {
        HAL_UART_Transmit_DMA(handle[device], data, len);
    } else {
        HAL_UART_Transmit_IT(handle[device], data, len);
    }
}

void bsp_uart_printf(bsp_uart_e device, const char *fmt, ...) {
    BSP_ASSERT(0 <= device && device < BSP_UART_DEVICE_COUNT);
    va_list ap;
    va_start(ap, fmt);
    uint8_t *buf = tx_buffer[device];
    const int len = vsnprintf((char *) buf, BSP_UART_BUFFER_SIZE, fmt, ap);
    va_end(ap);
    BSP_ASSERT(0 < len && len <= BSP_UART_BUFFER_SIZE);
    bsp_uart_send(device, buf, len);
}

void bsp_uart_printf_async(bsp_uart_e device, const char *fmt, ...) {
    BSP_ASSERT(0 <= device && device < BSP_UART_DEVICE_COUNT);
    if (lock[device]) return;
    va_list ap;
    va_start(ap, fmt);
    uint8_t *buf = tx_buffer[device];
    const int len = vsnprintf((char *) buf, BSP_UART_BUFFER_SIZE, fmt, ap);
    va_end(ap);
    BSP_ASSERT(0 < len && len <= BSP_UART_BUFFER_SIZE);
    bsp_uart_send_async(device, buf, len);
}

void bsp_uart_set_callback(bsp_uart_e device, bsp_uart_callback_t func) {
    BSP_ASSERT(0 <= device && device < BSP_UART_DEVICE_COUNT && callback[device] == NULL && func != NULL);

    callback[device] = func;

    HAL_UARTEx_ReceiveToIdle_DMA(handle[device], rx_buffer[device], BSP_UART_BUFFER_SIZE);
    __HAL_DMA_DISABLE_IT(handle[device]->hdmarx, DMA_IT_HT);
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *h, uint16_t len) {
    for (int i = 0; i < BSP_UART_DEVICE_COUNT; i++) {
        if (handle[i] == h) {
            if (callback[i] != NULL) {
                callback[i](i, rx_buffer[i], len);
                memset(rx_buffer[i], 0, sizeof(uint8_t) * len);
                HAL_UARTEx_ReceiveToIdle_DMA(h, rx_buffer[i], BSP_UART_BUFFER_SIZE);
                __HAL_DMA_DISABLE_IT(h->hdmarx, DMA_IT_HT);
            }
            break;
        }
    }
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *h) {
    for (int i = 0; i < BSP_UART_DEVICE_COUNT; i++) {
        if (handle[i] == h) {
            lock[i] = 0;
            break;
        }
    }
}