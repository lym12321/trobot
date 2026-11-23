#include "bsp/uart.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "usart.h"
#include "ring.h"
#include "bsp/sys.h"
#include "bsp/time.h"

typedef enum {
    UART_TX_IDLE,
    UART_TX_ASYNC,
    UART_TX_BLOCKING,
} uart_tx_state_e;

static bsp_uart_handle_t *const handles[BSP_UART_DEVICE_COUNT] = {
    [E_UART_1] = &huart1,
    [E_UART_2] = &huart2,
    [E_UART_3] = &huart3,
    [E_UART_5] = &huart5,
    [E_UART_7] = &huart7,
    [E_UART_10] = &huart10,
};

static bsp_frame_ring_t tx_queues[BSP_UART_DEVICE_COUNT];
static uint8_t tx_buffers[BSP_UART_DEVICE_COUNT][BSP_SERIAL_BUFFER_SIZE];
static uint8_t rx_buffers[BSP_UART_DEVICE_COUNT][2][BSP_SERIAL_BUFFER_SIZE];
static uint8_t rx_active[BSP_UART_DEVICE_COUNT];
static uart_tx_state_e tx_states[BSP_UART_DEVICE_COUNT];
static bsp_serial_callback_t callbacks[BSP_UART_DEVICE_COUNT];
static bsp_uart_stats_t stats[BSP_UART_DEVICE_COUNT];

static bool uart_device_valid(bsp_serial_t device) {
    return (unsigned) device < BSP_UART_DEVICE_COUNT;
}

static void uart_dma_error(DMA_HandleTypeDef *dma) {
    UART_HandleTypeDef *const handle = dma->Parent;
    /* HAL's shared DMA error callback marks both halves ready, even when the other stream is live. */
    if (dma == handle->hdmatx) {
        (void) HAL_UART_AbortTransmit_IT(handle);
    } else {
        (void) HAL_UART_AbortReceive_IT(handle);
    }
}

static HAL_StatusTypeDef uart_start_tx(uint8_t index, const uint8_t *data, uint16_t len) {
    if (handles[index]->hdmatx != NULL) {
        const HAL_StatusTypeDef status = HAL_UART_Transmit_DMA(handles[index], data, len);
        if (status == HAL_OK) handles[index]->hdmatx->XferErrorCallback = uart_dma_error;
        return status;
    }
    return HAL_UART_Transmit_IT(handles[index], data, len);
}

static bsp_status_t uart_start_rx(uint8_t index, uint8_t next) {
    bsp_uart_handle_t *const handle = handles[index];
    const bsp_status_t status = bsp_status_from_hal(
        HAL_UARTEx_ReceiveToIdle_DMA(handle, rx_buffers[index][next], BSP_SERIAL_BUFFER_SIZE)
    );
    if (status == BSP_STATUS_OK) {
        __HAL_DMA_DISABLE_IT(handle->hdmarx, DMA_IT_HT);
        handle->hdmarx->XferErrorCallback = uart_dma_error;
        rx_active[index] = next;
    } else {
        stats[index].rx_error_count++;
    }
    stats[index].last_status = status;
    return status;
}

static bsp_status_t uart_vprintf(
    bsp_uart_e device,
    const char *fmt,
    va_list args,
    bool blocking
) {
    uint8_t buffer[BSP_SERIAL_BUFFER_SIZE];
    const int length = vsnprintf((char *) buffer, sizeof(buffer), fmt, args);
    if (length <= 0) return BSP_STATUS_ERROR;
    const size_t send_length = length < (int) sizeof(buffer)
        ? (size_t) length
        : sizeof(buffer) - 1U;
    return blocking
        ? bsp_uart_send(device, buffer, (uint32_t) send_length)
        : bsp_uart_send_async(device, buffer, (uint32_t) send_length);
}

bsp_status_t bsp_uart_send(bsp_uart_e device, const uint8_t *data, uint32_t len) {
    if (!uart_device_valid(device) || data == NULL || len == 0 || len > UINT16_MAX) return BSP_STATUS_ERROR;
    unsigned long state = bsp_sys_enter_critical();
    if (tx_states[device] != UART_TX_IDLE) {
        stats[device].last_status = BSP_STATUS_BUSY;
        stats[device].tx_error_count++;
        bsp_sys_exit_critical(state);
        return BSP_STATUS_BUSY;
    }
    tx_states[device] = UART_TX_BLOCKING;
    bsp_sys_exit_critical(state);
    const bsp_status_t status = bsp_status_from_hal(
        HAL_UART_Transmit(handles[device], data, (uint16_t) len, HAL_MAX_DELAY)
    );
    state = bsp_sys_enter_critical();
    tx_states[device] = UART_TX_IDLE;
    stats[device].last_status = status;
    if (status != BSP_STATUS_OK) stats[device].tx_error_count++;
    bsp_sys_exit_critical(state);
    return status;
}

bsp_status_t bsp_uart_send_async(bsp_uart_e device, const uint8_t *data, uint32_t len) {
    if (!uart_device_valid(device) || data == NULL || len == 0 || len > BSP_UART_BUFFER_SIZE) return BSP_STATUS_ERROR;
    const unsigned long state = bsp_sys_enter_critical();
    bsp_status_t status = BSP_STATUS_OK;
    if (tx_states[device] == UART_TX_IDLE) {
        memcpy(tx_buffers[device], data, len);
        tx_states[device] = UART_TX_ASYNC;
        status = bsp_status_from_hal(uart_start_tx(device, tx_buffers[device], (uint16_t) len));
        if (status != BSP_STATUS_OK) {
            tx_states[device] = UART_TX_IDLE;
            stats[device].tx_error_count++;
        }
    } else if (tx_states[device] == UART_TX_BLOCKING) {
        status = BSP_STATUS_BUSY;
        stats[device].tx_error_count++;
    } else if (!bsp_frame_ring_push(&tx_queues[device], data, (uint16_t) len)) {
        status = BSP_STATUS_BUSY;
        stats[device].tx_drop_count++;
    }
    stats[device].last_status = status;
    bsp_sys_exit_critical(state);
    return status;
}

bsp_status_t bsp_uart_printf(bsp_uart_e device, const char *fmt, ...) {
    if (!uart_device_valid(device) || fmt == NULL) return BSP_STATUS_ERROR;
    va_list args;
    va_start(args, fmt);
    const bsp_status_t status = uart_vprintf(device, fmt, args, true);
    va_end(args);
    return status;
}

bsp_status_t bsp_uart_printf_async(bsp_uart_e device, const char *fmt, ...) {
    if (!uart_device_valid(device) || fmt == NULL) return BSP_STATUS_ERROR;
    va_list args;
    va_start(args, fmt);
    const bsp_status_t status = uart_vprintf(device, fmt, args, false);
    va_end(args);
    return status;
}

bsp_status_t bsp_uart_set_callback(bsp_uart_e device, bsp_serial_callback_t callback) {
    if (!uart_device_valid(device) || callback == NULL) return BSP_STATUS_ERROR;
    bsp_uart_handle_t *const handle = handles[device];
    if (handle->hdmarx == NULL) return BSP_STATUS_ERROR;

    const unsigned long state = bsp_sys_enter_critical();
    if (callbacks[device] != NULL && (callbacks[device] != callback ||
        handle->RxState != HAL_UART_STATE_READY || HAL_DMA_GetState(handle->hdmarx) != HAL_DMA_STATE_READY)) {
        bsp_sys_exit_critical(state);
        return BSP_STATUS_BUSY;
    }
    const uint32_t receiver_enabled = READ_BIT(handle->Instance->CR1, USART_CR1_RE);
    CLEAR_BIT(handle->Instance->CR1, USART_CR1_RE);
    __HAL_UART_CLEAR_FLAG(handle, UART_CLEAR_PEF | UART_CLEAR_FEF | UART_CLEAR_NEF | UART_CLEAR_OREF);
    __HAL_UART_SEND_REQ(handle, UART_RXDATA_FLUSH_REQUEST);
    const bsp_status_t status = uart_start_rx(device, rx_active[device]);
    if (status == BSP_STATUS_OK) callbacks[device] = callback;
    if (receiver_enabled != 0U) SET_BIT(handle->Instance->CR1, USART_CR1_RE);

    bsp_sys_exit_critical(state);
    return status;
}

bsp_status_t bsp_uart_set_baudrate(bsp_uart_e device, uint32_t baudrate) {
    if (!uart_device_valid(device) || baudrate == 0) return BSP_STATUS_ERROR;
    if (callbacks[device] != NULL) return BSP_STATUS_BUSY;

    const uint32_t started = bsp_time_get_ms();
    HAL_UART_StateTypeDef state = HAL_UART_GetState(handles[device]);
    while (state == HAL_UART_STATE_BUSY_TX || state == HAL_UART_STATE_BUSY_RX ||
           state == HAL_UART_STATE_BUSY_TX_RX) {
        if (bsp_time_get_ms() - started >= 100U) {
            const unsigned long lock = bsp_sys_enter_critical();
            stats[device].last_status = BSP_STATUS_TIMEOUT;
            bsp_sys_exit_critical(lock);
            return BSP_STATUS_TIMEOUT;
        }
        state = HAL_UART_GetState(handles[device]);
        bsp_time_delay(1);
    }

    handles[device]->Init.BaudRate = baudrate;
    const bsp_status_t status = bsp_status_from_hal(HAL_UART_Init(handles[device]));
    const unsigned long lock = bsp_sys_enter_critical();
    stats[device].last_status = status;
    bsp_sys_exit_critical(lock);
    return status;
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *handle, uint16_t length) {
    for (uint8_t index = 0; index < BSP_UART_DEVICE_COUNT; index++) {
        if (handles[index] != handle) continue;
        const bsp_serial_callback_t callback = callbacks[index];
        if (callback == NULL) return;

        const uint8_t completed = rx_active[index];
        (void) uart_start_rx(index, completed ^ 1U);
        callback((bsp_serial_t) index, rx_buffers[index][completed], length);
        return;
    }
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *handle) {
    for (uint8_t index = 0; index < BSP_UART_DEVICE_COUNT; index++) {
        if (handles[index] != handle) continue;

        uint16_t length = 0;
        const unsigned long state = bsp_sys_enter_critical();
        if (tx_states[index] == UART_TX_ASYNC) {
            if (bsp_frame_ring_pop(&tx_queues[index], tx_buffers[index], &length)) {
                const bsp_status_t status = bsp_status_from_hal(uart_start_tx(index, tx_buffers[index], length));
                stats[index].last_status = status;
                if (status != BSP_STATUS_OK) {
                    stats[index].tx_error_count++;
                    bsp_frame_ring_reset(&tx_queues[index]);
                    tx_states[index] = UART_TX_IDLE;
                }
            } else {
                tx_states[index] = UART_TX_IDLE;
            }
        }
        bsp_sys_exit_critical(state);
        return;
    }
}

void HAL_UART_AbortTransmitCpltCallback(UART_HandleTypeDef *handle) {
    for (uint8_t index = 0; index < BSP_UART_DEVICE_COUNT; index++) {
        if (handles[index] != handle) continue;

        const unsigned long state = bsp_sys_enter_critical();
        bsp_frame_ring_reset(&tx_queues[index]);
        tx_states[index] = UART_TX_IDLE;
        stats[index].tx_error_count++;
        stats[index].last_status = BSP_STATUS_ERROR;
        bsp_sys_exit_critical(state);
        return;
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *handle) {
    for (uint8_t index = 0; index < BSP_UART_DEVICE_COUNT; index++) {
        if (handles[index] != handle || callbacks[index] == NULL) continue;
        stats[index].rx_error_count++;
        stats[index].last_status = BSP_STATUS_ERROR;
        if (handle->RxState == HAL_UART_STATE_READY && HAL_DMA_GetState(handle->hdmarx) == HAL_DMA_STATE_READY) {
            const uint32_t receiver_enabled = READ_BIT(handle->Instance->CR1, USART_CR1_RE);
            CLEAR_BIT(handle->Instance->CR1, USART_CR1_RE);
            __HAL_UART_CLEAR_FLAG(handle, UART_CLEAR_PEF | UART_CLEAR_FEF | UART_CLEAR_NEF | UART_CLEAR_OREF);
            __HAL_UART_SEND_REQ(handle, UART_RXDATA_FLUSH_REQUEST);
            (void) uart_start_rx(index, rx_active[index]);
            if (receiver_enabled != 0U) SET_BIT(handle->Instance->CR1, USART_CR1_RE);
        }
        return;
    }
}

void HAL_UART_AbortReceiveCpltCallback(UART_HandleTypeDef *handle) {
    HAL_UART_ErrorCallback(handle);
}

bsp_uart_stats_t bsp_uart_stats(bsp_uart_e device) {
    if (!uart_device_valid(device)) return (bsp_uart_stats_t) { .last_status = BSP_STATUS_ERROR };
    const unsigned long state = bsp_sys_enter_critical();
    const bsp_uart_stats_t snapshot = stats[device];
    bsp_sys_exit_critical(state);
    return snapshot;
}
