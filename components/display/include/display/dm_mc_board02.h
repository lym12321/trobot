#pragma once

#include "bsp/display.h"
#include "bsp/time.h"
#include "display/st7789.h"

namespace display::dm_mc_board02 {

inline status_e transfer_status(bsp_status_t status) {
    if (status == BSP_STATUS_OK) return status_e::ok;
    if (status == BSP_STATUS_TIMEOUT) return status_e::timeout;
    return status_e::io_error;
}

inline constexpr port_t lcd_port{
    .ctx = nullptr,
    .reset = [](void *) { bsp_display_reset(); },
    .delay_ms = [](void *, uint32_t ms) { bsp_time_delay(ms); },
    .backlight = [](void *, bool enabled) { bsp_display_backlight(enabled); },
    .command = [](void *, uint8_t cmd, std::span<const uint8_t> args) {
        return transfer_status(bsp_display_command(cmd, args.data(), args.size()));
    },
    .data = [](void *, std::span<const uint8_t> bytes) {
        return transfer_status(bsp_display_data(bytes.data(), bytes.size()));
    },
};

}
