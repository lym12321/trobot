#pragma once

#include <stdint.h>

#include "bsp/def.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Task-context, single-writer debug output through SEGGER RTT channel 0. */
bsp_status_t bsp_rtt_write(const uint8_t *data, uint32_t len);
bsp_status_t bsp_rtt_printf(const char *fmt, ...);

#ifdef __cplusplus
}
#endif
