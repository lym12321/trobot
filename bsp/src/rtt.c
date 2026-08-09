#include "bsp/rtt.h"

#include <stdarg.h>
#include <stdio.h>

#include "SEGGER_RTT.h"

#define RTT_TERMINAL_CHANNEL 0U
#define RTT_PRINTF_BUFFER_SIZE 256U

bsp_status_t bsp_rtt_write(const uint8_t *data, const uint32_t len) {
    if (data == NULL || len == 0U) return BSP_STATUS_ERROR;

    const unsigned written = SEGGER_RTT_Write(RTT_TERMINAL_CHANNEL, data, len);
    return written == len ? BSP_STATUS_OK : BSP_STATUS_BUSY;
}

bsp_status_t bsp_rtt_printf(const char *fmt, ...) {
    if (fmt == NULL) return BSP_STATUS_ERROR;

    char buffer[RTT_PRINTF_BUFFER_SIZE];
    va_list args;
    va_start(args, fmt);
    const int len = vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    if (len <= 0) return BSP_STATUS_ERROR;

    const uint32_t write_len = len < (int) sizeof(buffer) ? (uint32_t) len : sizeof(buffer) - 1U;
    return bsp_rtt_write((const uint8_t *) buffer, write_len);
}
