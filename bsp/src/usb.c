//
// Created by fish on 2026/1/6.
//

#include "bsp/usb.h"
#include "bsp/def.h"
#include "bsp/sys.h"

// Warning: must include from "usb_device/..." path
// It is stored at /bsp/internal/usb_device
#include "usb_device/usb_device.h"
#include "usb_device/usbd_cdc_if.h"

extern USBD_HandleTypeDef hUsbDeviceHS;

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static uint8_t tx_buffer[2][BSP_USB_CDC_BUFFER_SIZE];
static uint8_t next_tx_buffer;
static volatile uint32_t tx_drop_count;

void bsp_usb_init() {
    MX_USB_DEVICE_Init();
}

bsp_status_t bsp_usb_cdc_send(const uint8_t *data, uint16_t len) {
    if (data == NULL || len == 0 || len > BSP_USB_CDC_BUFFER_SIZE) return BSP_STATUS_ERROR;

    const unsigned long state = bsp_sys_enter_critical();
    USBD_CDC_HandleTypeDef *const hcdc =
        (USBD_CDC_HandleTypeDef *) hUsbDeviceHS.pClassData;
    if (hcdc == NULL || hcdc->TxState != 0U) {
        tx_drop_count++;
        bsp_sys_exit_critical(state);
        return BSP_STATUS_BUSY;
    }

    uint8_t *const buffer = tx_buffer[next_tx_buffer];
    memcpy(buffer, data, len);
    const uint8_t usb_status = CDC_Transmit_HS(buffer, len);
    if (usb_status == USBD_OK) {
        next_tx_buffer ^= 1U;
    } else {
        tx_drop_count++;
    }
    bsp_sys_exit_critical(state);
    return usb_status == USBD_OK ? BSP_STATUS_OK : usb_status == USBD_BUSY ? BSP_STATUS_BUSY : BSP_STATUS_ERROR;
}

bsp_status_t bsp_usb_cdc_printf(const char *fmt, ...) {
    if (fmt == NULL) return BSP_STATUS_ERROR;
    va_list args;
    va_start(args, fmt);
    uint8_t buf[BSP_USB_CDC_BUFFER_SIZE];
    const int len = vsnprintf((char *) buf, sizeof(buf), fmt, args);
    va_end(args);
    if (len > 0) {
        const uint16_t send_len = len < (int) sizeof(buf) ? (uint16_t) len : sizeof(buf) - 1;
        return bsp_usb_cdc_send(buf, send_len);
    }
    return BSP_STATUS_ERROR;
}
