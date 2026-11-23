#include "display/st7789.h"

#include <algorithm>

namespace display {

static constexpr uint8_t setup_240x280[] = {
    0xB2, 5, 0x0C, 0x0C, 0x00, 0x33, 0x33,
    0xB7, 1, 0x35,
    0xBB, 1, 0x32,
    0xC2, 1, 0x01,
    0xC3, 1, 0x15,
    0xC4, 1, 0x20,
    0xC6, 1, 0x0F,
    0xD0, 2, 0xA4, 0xA1,
    0xE0, 14, 0xD0, 0x08, 0x0E, 0x09, 0x09, 0x05, 0x31,
              0x33, 0x48, 0x17, 0x14, 0x15, 0x31, 0x34,
    0xE1, 14, 0xD0, 0x08, 0x0E, 0x09, 0x09, 0x15, 0x31,
              0x33, 0x48, 0x17, 0x14, 0x15, 0x31, 0x34,
};

constinit const panel_t panel_240x280{240, 280, 0, 20, 0x00, true, setup_240x280};

status_e st7789::command(uint8_t cmd, std::span<const uint8_t> args) {
    return port_.command(port_.ctx, cmd, args);
}

status_e st7789::init() {
    ready_ = false;
    port_.backlight(port_.ctx, false);
    const bool swap_xy = (panel_.madctl & 0x20U) != 0;
    if (panel_.width == 0 || panel_.height == 0 ||
        panel_.x_offset + panel_.width > (swap_xy ? 320 : 240) ||
        panel_.y_offset + panel_.height > (swap_xy ? 240 : 320)) return status_e::invalid;

    port_.reset(port_.ctx);
    auto status = command(0x11); // SLPOUT
    if (status != status_e::ok) return status;
    port_.delay_ms(port_.ctx, 120);
    status = command(0x36, {&panel_.madctl, 1});
    if (status != status_e::ok) return status;
    constexpr uint8_t rgb565_format = 0x05;
    status = command(0x3A, {&rgb565_format, 1});
    if (status != status_e::ok) return status;
    auto setup = panel_.setup;
    while (!setup.empty()) {
        if (setup.size() < 2 || setup[1] > setup.size() - 2) return status_e::invalid;
        const size_t len = setup[1];
        status = command(setup[0], setup.subspan(2, len));
        if (status != status_e::ok) return status;
        setup = setup.subspan(2 + len);
    }
    status = command(panel_.inverted ? 0x21 : 0x20);
    if (status != status_e::ok) return status;
    status = command(0x29); // DISPON
    if (status != status_e::ok) return status;
    ready_ = true;
    status = clear(0);
    ready_ = status == status_e::ok;
    if (ready_) port_.backlight(port_.ctx, true);
    return status;
}

bool st7789::contains(rect_t area) const {
    return area.width && area.height && area.x + area.width <= panel_.width &&
           area.y + area.height <= panel_.height;
}

status_e st7789::window(rect_t area) {
    if (!ready_ || !contains(area)) return status_e::invalid;
    const uint16_t x = panel_.x_offset + area.x;
    const uint16_t y = panel_.y_offset + area.y;
    const uint16_t right = x + area.width - 1;
    const uint16_t bottom = y + area.height - 1;
    const uint8_t col[] = {uint8_t(x >> 8), uint8_t(x), uint8_t(right >> 8), uint8_t(right)};
    const uint8_t row[] = {uint8_t(y >> 8), uint8_t(y), uint8_t(bottom >> 8), uint8_t(bottom)};
    auto status = command(0x2A, col);
    if (status != status_e::ok) return status;
    status = command(0x2B, row);
    if (status != status_e::ok) return status;
    return command(0x2C);
}

status_e st7789::write_pixels(std::span<const uint8_t> bytes) {
    return port_.data(port_.ctx, bytes);
}

status_e st7789::fill(rect_t area, color_t color) {
    auto status = window(area);
    if (status != status_e::ok) return status;
    uint32_t remaining = uint32_t(area.width) * area.height * 2;
    uint8_t buf[chunk_size];
    const size_t len = std::min<size_t>(remaining, sizeof(buf));
    for (size_t i = 0; i < len; i += 2) {
        buf[i] = uint8_t(color >> 8);
        buf[i + 1] = uint8_t(color);
    }
    while (remaining) {
        const size_t n = std::min<size_t>(remaining, len);
        status = write_pixels({buf, n});
        if (status != status_e::ok) return status;
        remaining -= n;
    }
    return status_e::ok;
}

status_e st7789::blit(rect_t area, std::span<const color_t> pixels) {
    if (pixels.size() != size_t(area.width) * area.height) return status_e::invalid;
    auto status = window(area);
    if (status != status_e::ok) return status;
    uint8_t buf[chunk_size];
    while (!pixels.empty()) {
        const size_t n = std::min(pixels.size(), sizeof(buf) / 2);
        for (size_t i = 0; i < n; ++i) {
            buf[2 * i] = uint8_t(pixels[i] >> 8);
            buf[2 * i + 1] = uint8_t(pixels[i]);
        }
        status = write_pixels({buf, n * 2});
        if (status != status_e::ok) return status;
        pixels = pixels.subspan(n);
    }
    return status_e::ok;
}

}
