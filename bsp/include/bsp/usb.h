#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bsp/serial.h"

#ifndef USB_WINUSB_ENABLE
#define USB_WINUSB_ENABLE 0
#endif

#define BSP_USB_CDC_COUNT USB_CDC_COUNT
#define BSP_USB_CDC_BUFFER_SIZE BSP_SERIAL_BUFFER_SIZE
#define BSP_USB_WINUSB_REQUEST_SIZE 64U
#define BSP_USB_WINUSB_RESPONSE_SIZE 16384U

#ifdef __cplusplus
extern "C" {
#endif

/* USB RX callbacks execute in the USB ISR and must be bounded and non-blocking. */
typedef void (*bsp_usb_cdc_callback_t)(const uint8_t *data, size_t len);
typedef void (*bsp_usb_winusb_callback_t)(const uint8_t *data, size_t len);

/* Each block is 512 bytes. The disk and its context must outlive USB.
 * read runs synchronously in the task calling bsp_usb_poll(). */
typedef struct {
    uint32_t block_count;
    bsp_status_t (*read)(void *ctx, uint32_t lba, uint8_t *dst, size_t blocks);
    void *ctx;
} bsp_usb_disk_t;

typedef struct {
    uint8_t cdc_count;
    bool winusb;
    const bsp_usb_disk_t *disk;
} bsp_usb_config_t;

/* A zeroed config leaves USB disconnected.
 * CDC capacity is max(1, USB_CDC_COUNT). WinUSB requires USB_WINUSB_ENABLE. */
bsp_status_t bsp_usb_init(const bsp_usb_config_t *config);
/* A single task must poll while a disk is attached. Each call serves at most one sector. */
void bsp_usb_poll(void);
bool bsp_usb_is_configured(void);
uint8_t bsp_usb_cdc_count(void);

/* Single-port CDC entry points use CDC 0. Its handler can be replaced before or after enumeration. */
bsp_status_t bsp_usb_cdc_send(const uint8_t *data, uint16_t len);
bsp_status_t bsp_usb_cdc_printf(const char *fmt, ...);
bsp_status_t bsp_usb_cdc_set_callback(bsp_usb_cdc_callback_t callback);

/* Keep the 4-byte-aligned TX buffer valid and unchanged until bsp_usb_winusb_tx_busy() is false. */
bsp_status_t bsp_usb_winusb_send(const uint8_t *data, uint16_t len);
bool bsp_usb_winusb_tx_busy(void);
/* The RX pointer is valid only for the duration of this ISR callback. */
bsp_status_t bsp_usb_winusb_set_callback(bsp_usb_winusb_callback_t callback);

const char *bsp_usb_serial_number(void);

#ifdef __cplusplus
}
#endif
