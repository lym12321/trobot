#pragma once

#include <stddef.h>
#include <stdint.h>
#include "bsp/def.h"

#ifndef USB_CDC_COUNT
#define USB_CDC_COUNT 0
#endif

#define BSP_SERIAL_BUFFER_SIZE 512

typedef enum {
    E_UART_1,
    E_UART_2,
    E_UART_3,
    E_UART_5,
    E_UART_7,
    E_UART_10,
    E_USB_CDC_0,
#if USB_CDC_COUNT > 1
    E_USB_CDC_1,
#endif
#if USB_CDC_COUNT > 2
    E_USB_CDC_2,
#endif
    BSP_SERIAL_DEVICE_COUNT
} bsp_uart_e;

typedef bsp_uart_e bsp_serial_t;

#ifdef __cplusplus
extern "C" {
#endif

/* RX runs in the port ISR. Use bounded, non-blocking work. Data expires on return. */
typedef void (*bsp_serial_callback_t)(bsp_serial_t port, const uint8_t *data, size_t len);

/* OK copies the entire frame. BUSY drops this submission, OFFLINE means CDC is unavailable.
 * A peripheral fault or USB disconnect discards pending output. */
bsp_status_t bsp_serial_send_async(bsp_serial_t port, const uint8_t *data, size_t len);
/* Register one RX owner per port during initialization. CDC registration can precede enumeration. */
bsp_status_t bsp_serial_set_callback(bsp_serial_t port, bsp_serial_callback_t callback);
/* Compare across RX chunks. CDC changes on bus reset, configuration or data-interface restart. */
uint32_t bsp_serial_session(bsp_serial_t port);

#ifdef __cplusplus
}
#endif
