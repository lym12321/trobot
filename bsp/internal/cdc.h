#pragma once

#include "bsp/serial.h"

bsp_status_t bsp_cdc_send(uint8_t index, const uint8_t *data, size_t len);
bsp_status_t bsp_cdc_set_callback(uint8_t index, bsp_serial_callback_t callback);
uint32_t bsp_cdc_session(uint8_t index);
