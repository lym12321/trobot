//
// Created by fish on 2026/1/6.
//

#include "bsp/usb.h"

// Warning: must include from "usb_device/..." path
// It is stored at /bsp/internal/usb_device
#include "usb_device/usb_device.h"
#include "usb_device/usbd_cdc_if.h"

#include <stdarg.h>

uint8_t buf[256];

void bsp_usb_init() {
    MX_USB_DEVICE_Init();
}

void bsp_usb_cdc_send(uint8_t *data, uint16_t len) {
    uint8_t ret;
    do {
        ret = CDC_Transmit_HS(data, len);
    } while (ret == USBD_BUSY);
}

void bsp_usb_cdc_send_async(uint8_t *data, uint16_t len) {

}

void bsp_usb_cdc_printf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf((char *) buf, sizeof(buf), fmt, args);
    va_end(args);
    if (len > 0) {
        bsp_usb_cdc_send(buf, (uint16_t)len);
    }
}
