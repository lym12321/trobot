#pragma once

#include <stdint.h>

#include "bsp/def.h"

#ifdef __cplusplus
extern "C" {
#endif

bsp_status_t bsp_rtt_write(const uint8_t *data, uint32_t len);
bsp_status_t bsp_rtt_printf(const char *fmt, ...);

#ifdef __cplusplus
}
#endif
