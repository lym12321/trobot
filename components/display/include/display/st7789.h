#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace display {

using color_t = uint16_t;

constexpr color_t rgb565(uint8_t red, uint8_t green, uint8_t blue) {
    return static_cast<color_t>((red & 0xF8U) << 8 | (green & 0xFCU) << 3 | blue >> 3);
}

enum class status_e : uint8_t { ok, invalid, io_error, timeout };
enum class font_e : uint8_t { classic, block };

struct rect_t {
    uint16_t x, y, width, height;
};

struct port_t {
    void *ctx;
    void (*reset)(void *ctx);
    void (*delay_ms)(void *ctx, uint32_t ms);
    void (*backlight)(void *ctx, bool enabled);
    status_e (*command)(void *ctx, uint8_t cmd, std::span<const uint8_t> args);
    status_e (*data)(void *ctx, std::span<const uint8_t> bytes);
};

struct panel_t {
    uint16_t width, height, x_offset, y_offset;
    uint8_t madctl;
    bool inverted;
    // Opcode, payload length, payload bytes. The table must outlive the screen.
    std::span<const uint8_t> setup;
};

extern const panel_t panel_240x280;

class st7789 {
public:
    constexpr st7789(const port_t& port, const panel_t& panel) : port_(port), panel_(panel) {}
    st7789(const port_t&&, const panel_t&) = delete;
    st7789(const port_t&, const panel_t&&) = delete;
    st7789(const st7789&) = delete;
    st7789& operator=(const st7789&) = delete;

    [[nodiscard]] status_e init();
    [[nodiscard]] status_e clear(color_t color) { return fill({0, 0, panel_.width, panel_.height}, color); }
    [[nodiscard]] status_e fill(rect_t area, color_t color);
    [[nodiscard]] status_e blit(rect_t area, std::span<const color_t> pixels);
    [[nodiscard]] status_e pixel(uint16_t x, uint16_t y, color_t color);
    [[nodiscard]] status_e line(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, color_t color);
    [[nodiscard]] status_e rect(rect_t area, color_t color);
    [[nodiscard]] status_e circle(uint16_t x, uint16_t y, uint16_t radius, color_t color);
    [[nodiscard]] status_e text(uint16_t x, uint16_t y, std::string_view chars,
                                color_t fg = 0xFFFF, color_t bg = 0,
                                uint8_t scale = 1, font_e font = font_e::classic);

private:
    static constexpr size_t chunk_size = 128;
    const port_t& port_;
    const panel_t& panel_;
    bool ready_ = false;

    bool contains(rect_t area) const;
    status_e command(uint8_t cmd, std::span<const uint8_t> args = {});
    status_e window(rect_t area);
    status_e write_pixels(std::span<const uint8_t> bytes);
};

}
