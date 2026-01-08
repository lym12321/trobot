//
// Created by fish on 2026/1/6.
//

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void bsp_usb_init();
void bsp_usb_cdc_send(uint8_t *data, uint16_t len);
void bsp_usb_cdc_printf(const char *fmt, ...);

#ifdef __cplusplus
}
#endif