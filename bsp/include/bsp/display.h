#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "bsp/def.h"

#ifdef __cplusplus
extern "C" {
#endif

void bsp_display_reset(void);
void bsp_display_backlight(bool enabled);
bsp_status_t bsp_display_command(uint8_t cmd, const uint8_t *args, size_t len);
bsp_status_t bsp_display_data(const uint8_t *bytes, size_t len);

#ifdef __cplusplus
}
#endif
