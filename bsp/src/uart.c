//
// Created by fish on 2025/9/19.
//

#include "bsp/uart.h"
#include "bsp/ds.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "usart.h"
#include "bsp/sys.h"
#include "bsp/time.h"

static bsp_uart_handle_t *handle[BSP_UART_DEVICE_COUNT] = {
    [E_UART_1] = &huart1,
    [E_UART_2] = &huart2,
    [E_UART_3] = &huart3,
    [E_UART_5] = &huart5,
    [E_UART_7] = &huart7,
    [E_UART_10] = &huart10
};

// Ring Queue for async transmission
#define BSP_UART_QUEUE_SIZE (BSP_UART_BUFFER_SIZE * 4)
static ds_rq_t rq[BSP_UART_DEVICE_COUNT];
static volatile uint8_t busy[BSP_UART_DEVICE_COUNT];
static uint8_t rq_buffer[BSP_UART_DEVICE_COUNT][BSP_UART_QUEUE_SIZE];

static bsp_uart_callback_t callback[BSP_UART_DEVICE_COUNT];
static uint8_t tx_buffer[BSP_UART_DEVICE_COUNT][BSP_UART_BUFFER_SIZE];
static uint8_t rx_buffer[BSP_UART_DEVICE_COUNT][2][BSP_UART_BUFFER_SIZE];
static uint8_t rx_active[BSP_UART_DEVICE_COUNT];
static volatile bsp_uart_stats_t stats[BSP_UART_DEVICE_COUNT];

bsp_status_t bsp_uart_send(bsp_uart_e device, const uint8_t *data, const uint32_t len) {
    if (device < 0 || device >= BSP_UART_DEVICE_COUNT || data == NULL || len == 0) return BSP_STATUS_ERROR;
    const bsp_status_t status = bsp_status_from_hal(HAL_UART_Transmit(handle[device], data, len, HAL_MAX_DELAY));
    stats[device].last_status = status;
    if (status != BSP_STATUS_OK) stats[device].tx_error_count++;
    return status;
}

bsp_status_t bsp_uart_send_async(bsp_uart_e device, const uint8_t *data, const uint32_t len) {
    if (device < 0 || device >= BSP_UART_DEVICE_COUNT || data == NULL || len == 0 || len > BSP_UART_BUFFER_SIZE) return BSP_STATUS_ERROR;

    uint8_t send_now = 0;
    unsigned int _state = bsp_sys_enter_critical();

    if (rq[device].buf != rq_buffer[device])
        ds_rq_init(&rq[device], rq_buffer[device], BSP_UART_QUEUE_SIZE);

    if (!busy[device]) {
        busy[device] = 2;                                               // 这里 busy 状态为 2 表示等待发送, 外设并未实际开始发送
        send_now = 1;
    } else {
        if (ds_rq_avail(&rq[device]) >= sizeof(len) + len) {
            ds_rq_push(&rq[device], (const uint8_t *) &len, sizeof(len));
            ds_rq_push(&rq[device], data, len);
        } else {
            stats[device].tx_drop_count++;
            stats[device].last_status = BSP_STATUS_BUSY;
            bsp_sys_exit_critical(_state);
            return BSP_STATUS_BUSY;
        }
    }

    bsp_sys_exit_critical(_state);

    if (send_now) {
        if (data != tx_buffer[device]) {
            memcpy(tx_buffer[device], data, len);
            data = tx_buffer[device];
        }
        HAL_StatusTypeDef st;
        if (handle[device]->hdmatx) {
            st = HAL_UART_Transmit_DMA(handle[device], data, len);
        } else {
            st = HAL_UART_Transmit_IT(handle[device], data, len);
        }
        _state = bsp_sys_enter_critical();
        // 发送完成回调可能已经在 HAL 调用期间执行；只有仍处于启动状态时
        // 才允许调用方把 busy 状态切换为发送中，避免覆盖回调设置的空闲状态。
        if (st == HAL_OK) {
            if (busy[device] == 2) busy[device] = 1;
        } else {
            if (busy[device] == 2) {
                busy[device] = 0;
                ds_rq_init(&rq[device], rq_buffer[device], BSP_UART_QUEUE_SIZE);
            }
            stats[device].tx_error_count++;
        }
        const bsp_status_t status = bsp_status_from_hal(st);
        stats[device].last_status = status;
        bsp_sys_exit_critical(_state);
        return status;
    }
    return BSP_STATUS_OK;
}

bsp_status_t bsp_uart_printf(bsp_uart_e device, const char *fmt, ...) {
    if (device < 0 || device >= BSP_UART_DEVICE_COUNT || fmt == NULL) return BSP_STATUS_ERROR;
    va_list ap;
    va_start(ap, fmt);
    uint8_t buf[BSP_UART_BUFFER_SIZE];
    const int len = vsnprintf((char *) buf, BSP_UART_BUFFER_SIZE, fmt, ap);
    va_end(ap);
    if (len <= 0) return BSP_STATUS_ERROR;
    const uint32_t send_len = len < BSP_UART_BUFFER_SIZE ? (uint32_t) len : BSP_UART_BUFFER_SIZE - 1;
    return bsp_uart_send(device, buf, send_len);
}

bsp_status_t bsp_uart_printf_async(bsp_uart_e device, const char *fmt, ...) {
    if (device < 0 || device >= BSP_UART_DEVICE_COUNT || fmt == NULL) return BSP_STATUS_ERROR;
    va_list ap;
    va_start(ap, fmt);
    uint8_t buf[BSP_UART_BUFFER_SIZE];
    const int len = vsnprintf((char *) buf, BSP_UART_BUFFER_SIZE, fmt, ap);
    va_end(ap);
    if (len <= 0) return BSP_STATUS_ERROR;
    const uint32_t send_len = len < BSP_UART_BUFFER_SIZE ? (uint32_t) len : BSP_UART_BUFFER_SIZE - 1;
    return bsp_uart_send_async(device, buf, send_len);
}

bsp_status_t bsp_uart_set_callback(bsp_uart_e device, bsp_uart_callback_t func) {
    BSP_ASSERT(0 <= device && device < BSP_UART_DEVICE_COUNT && callback[device] == NULL && func != NULL);
    BSP_ASSERT(handle[device]->hdmarx != NULL);     // 确保外设开启了 RX DMA

    rx_active[device] = 0;
    const bsp_status_t status = bsp_status_from_hal(
        HAL_UARTEx_ReceiveToIdle_DMA(handle[device], rx_buffer[device][rx_active[device]], BSP_UART_BUFFER_SIZE)
    );
    stats[device].last_status = status;
    if (status != BSP_STATUS_OK) {
        stats[device].rx_error_count++;
        return status;
    }
    callback[device] = func;
    __HAL_DMA_DISABLE_IT(handle[device]->hdmarx, DMA_IT_HT);
    return BSP_STATUS_OK;
}

// 不要在 set_callback 后执行 set_baudrate, 否则可能会破坏空闲中断状态
bsp_status_t bsp_uart_set_baudrate(bsp_uart_e device, uint32_t baudrate) {
    BSP_ASSERT(0 <= device && device < BSP_UART_DEVICE_COUNT && baudrate > 0 && callback[device] == NULL);

    const uint32_t start = bsp_time_get_ms();
    HAL_UART_StateTypeDef state = HAL_UART_GetState(handle[device]);
    while (state == HAL_UART_STATE_BUSY_TX || state == HAL_UART_STATE_BUSY_RX || state == HAL_UART_STATE_BUSY_TX_RX) {
        if (bsp_time_get_ms() - start >= 100) {
            stats[device].last_status = BSP_STATUS_TIMEOUT;
            return BSP_STATUS_TIMEOUT;
        }
        state = HAL_UART_GetState(handle[device]);
        bsp_time_delay(1);
    }

    HAL_StatusTypeDef hal = HAL_UART_DeInit(handle[device]);
    handle[device]->Init.BaudRate = baudrate;
    if (hal == HAL_OK) hal = HAL_UART_Init(handle[device]);
    const bsp_status_t status = bsp_status_from_hal(hal);
    stats[device].last_status = status;
    return status;
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *h, uint16_t len) {
    for (int i = 0; i < BSP_UART_DEVICE_COUNT; i++) {
        if (handle[i] == h) {
            if (callback[i] != NULL) {
                const uint8_t completed = rx_active[i];
                rx_active[i] ^= 1U;
                const bsp_status_t status = bsp_status_from_hal(
                    HAL_UARTEx_ReceiveToIdle_DMA(h, rx_buffer[i][rx_active[i]], BSP_UART_BUFFER_SIZE)
                );
                stats[i].last_status = status;
                if (status == BSP_STATUS_OK) __HAL_DMA_DISABLE_IT(h->hdmarx, DMA_IT_HT);
                else stats[i].rx_error_count++;
                callback[i](i, rx_buffer[i][completed], len);
            }
            break;
        }
    }
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *h) {
    for (int i = 0; i < BSP_UART_DEVICE_COUNT; i++) {
        if (handle[i] == h && busy[i]) {
            uint32_t len = 0;
            uint8_t send_nxt = 0;
            unsigned long _state = bsp_sys_enter_critical();
            if (ds_rq_size(&rq[i]) == 0) {
                busy[i] = 0;
            } else {
                ds_rq_pop(&rq[i], (uint8_t *) &len, sizeof(len));
                ds_rq_pop(&rq[i], tx_buffer[i], len);
                if (len == 0 || len > BSP_UART_BUFFER_SIZE) {
                    busy[i] = 0;
                    ds_rq_init(&rq[i], rq_buffer[i], BSP_UART_QUEUE_SIZE);
                } else {
                    send_nxt = 1;
                }
            }
            bsp_sys_exit_critical(_state);
            if (send_nxt) {
                HAL_StatusTypeDef st;
                if (handle[i]->hdmatx) {
                    st = HAL_UART_Transmit_DMA(handle[i], tx_buffer[i], len);
                } else {
                    st = HAL_UART_Transmit_IT(handle[i], tx_buffer[i], len);
                }
                if (st != HAL_OK) {
                    _state = bsp_sys_enter_critical();
                    ds_rq_init(&rq[i], rq_buffer[i], BSP_UART_QUEUE_SIZE);
                    busy[i] = 0;
                    stats[i].tx_error_count++;
                    stats[i].last_status = bsp_status_from_hal(st);
                    bsp_sys_exit_critical(_state);
                }
            }
            break;
        }
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *h) {
    for (int i = 0; i < BSP_UART_DEVICE_COUNT; i++) {
        if (handle[i] == h) {
            const uint8_t rx_configured = callback[i] != NULL && h->hdmarx != NULL;
            const unsigned long _state = bsp_sys_enter_critical();
            if ((h->ErrorCode & HAL_UART_ERROR_DMA) != 0U && busy[i]) {
                ds_rq_init(&rq[i], rq_buffer[i], BSP_UART_QUEUE_SIZE);
                busy[i] = 0;
                stats[i].tx_error_count++;
            }
            if (rx_configured) stats[i].rx_error_count++;
            stats[i].last_status = BSP_STATUS_ERROR;
            bsp_sys_exit_critical(_state);

            if (rx_configured) {
                rx_active[i] ^= 1U;
                const bsp_status_t status = bsp_status_from_hal(
                    HAL_UARTEx_ReceiveToIdle_DMA(h, rx_buffer[i][rx_active[i]], BSP_UART_BUFFER_SIZE)
                );
                stats[i].last_status = status;
                if (status == BSP_STATUS_OK) __HAL_DMA_DISABLE_IT(h->hdmarx, DMA_IT_HT);
                else stats[i].rx_error_count++;
            }
            break;
        }
    }
}

bsp_uart_stats_t bsp_uart_stats(bsp_uart_e device) {
    BSP_ASSERT(0 <= device && device < BSP_UART_DEVICE_COUNT);
    const unsigned long state = bsp_sys_enter_critical();
    const bsp_uart_stats_t copy = {
        .tx_error_count = stats[device].tx_error_count,
        .rx_error_count = stats[device].rx_error_count,
        .tx_drop_count = stats[device].tx_drop_count,
        .last_status = stats[device].last_status,
    };
    bsp_sys_exit_critical(state);
    return copy;
}
