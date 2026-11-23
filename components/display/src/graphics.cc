#include "display/st7789.h"

#include <algorithm>
#include <cstdlib>

namespace display {

constexpr unsigned glyph_width = 5;
constexpr unsigned glyph_height = 7;

constexpr uint8_t digits[][glyph_width] = {
    {0x3E, 0x51, 0x49, 0x45, 0x3E},
    {0x00, 0x42, 0x7F, 0x40, 0x00},
    {0x42, 0x61, 0x51, 0x49, 0x46},
    {0x21, 0x41, 0x45, 0x4B, 0x31},
    {0x18, 0x14, 0x12, 0x7F, 0x10},
    {0x27, 0x45, 0x45, 0x45, 0x39},
    {0x3C, 0x4A, 0x49, 0x49, 0x30},
    {0x01, 0x71, 0x09, 0x05, 0x03},
    {0x36, 0x49, 0x49, 0x49, 0x36},
    {0x06, 0x49, 0x49, 0x29, 0x1E},
};

constexpr uint8_t uppercase[][glyph_width] = {
    {0x7E, 0x11, 0x11, 0x11, 0x7E},
    {0x7F, 0x49, 0x49, 0x49, 0x36},
    {0x3E, 0x41, 0x41, 0x41, 0x22},
    {0x7F, 0x41, 0x41, 0x22, 0x1C},
    {0x7F, 0x49, 0x49, 0x49, 0x41},
    {0x7F, 0x09, 0x09, 0x09, 0x01},
    {0x3E, 0x41, 0x49, 0x49, 0x7A},
    {0x7F, 0x08, 0x08, 0x08, 0x7F},
    {0x00, 0x41, 0x7F, 0x41, 0x00},
    {0x20, 0x40, 0x41, 0x3F, 0x01},
    {0x7F, 0x08, 0x14, 0x22, 0x41},
    {0x7F, 0x40, 0x40, 0x40, 0x40},
    {0x7F, 0x02, 0x0C, 0x02, 0x7F},
    {0x7F, 0x04, 0x08, 0x10, 0x7F},
    {0x3E, 0x41, 0x41, 0x41, 0x3E},
    {0x7F, 0x09, 0x09, 0x09, 0x06},
    {0x3E, 0x41, 0x51, 0x21, 0x5E},
    {0x7F, 0x09, 0x19, 0x29, 0x46},
    {0x46, 0x49, 0x49, 0x49, 0x31},
    {0x01, 0x01, 0x7F, 0x01, 0x01},
    {0x3F, 0x40, 0x40, 0x40, 0x3F},
    {0x1F, 0x20, 0x40, 0x20, 0x1F},
    {0x3F, 0x40, 0x38, 0x40, 0x3F},
    {0x63, 0x14, 0x08, 0x14, 0x63},
    {0x07, 0x08, 0x70, 0x08, 0x07},
    {0x61, 0x51, 0x49, 0x45, 0x43},
};

constexpr uint8_t lowercase[][glyph_width] = {
    {0x20, 0x54, 0x54, 0x54, 0x78},
    {0x7F, 0x48, 0x44, 0x44, 0x38},
    {0x38, 0x44, 0x44, 0x44, 0x20},
    {0x38, 0x44, 0x44, 0x48, 0x7F},
    {0x38, 0x54, 0x54, 0x54, 0x18},
    {0x08, 0x7E, 0x09, 0x01, 0x02},
    {0x0C, 0x52, 0x52, 0x52, 0x3E},
    {0x7F, 0x08, 0x04, 0x04, 0x78},
    {0x00, 0x44, 0x7D, 0x40, 0x00},
    {0x20, 0x40, 0x44, 0x3D, 0x00},
    {0x7F, 0x10, 0x28, 0x44, 0x00},
    {0x00, 0x41, 0x7F, 0x40, 0x00},
    {0x7C, 0x04, 0x18, 0x04, 0x78},
    {0x7C, 0x08, 0x04, 0x04, 0x78},
    {0x38, 0x44, 0x44, 0x44, 0x38},
    {0x7C, 0x14, 0x14, 0x14, 0x08},
    {0x08, 0x14, 0x14, 0x18, 0x7C},
    {0x7C, 0x08, 0x04, 0x04, 0x08},
    {0x48, 0x54, 0x54, 0x54, 0x20},
    {0x04, 0x3F, 0x44, 0x40, 0x20},
    {0x3C, 0x40, 0x40, 0x20, 0x7C},
    {0x1C, 0x20, 0x40, 0x20, 0x1C},
    {0x3C, 0x40, 0x30, 0x40, 0x3C},
    {0x44, 0x28, 0x10, 0x28, 0x44},
    {0x0C, 0x50, 0x50, 0x50, 0x3C},
    {0x44, 0x64, 0x54, 0x4C, 0x44},
};

constexpr uint8_t space[glyph_width] = {0};
constexpr uint8_t question[glyph_width] = {
    0x02, 0x01, 0x51, 0x09, 0x06,
};
constexpr uint8_t greater[glyph_width] = {
    0x00, 0x41, 0x22, 0x14, 0x08,
};
constexpr uint8_t colon[glyph_width] = {
    0x00, 0x36, 0x36, 0x00, 0x00,
};
constexpr uint8_t minus[glyph_width] = {
    0x08, 0x08, 0x08, 0x08, 0x08,
};
constexpr uint8_t period[glyph_width] = {
    0x00, 0x60, 0x60, 0x00, 0x00,
};
constexpr uint8_t slash[glyph_width] = {
    0x20, 0x10, 0x08, 0x04, 0x02,
};

static const uint8_t *glyph_for(char character) {
    if (character >= '0' && character <= '9') {
        return digits[character - '0'];
    }
    if (character >= 'A' && character <= 'Z') {
        return uppercase[character - 'A'];
    }
    if (character >= 'a' && character <= 'z') {
        return lowercase[character - 'a'];
    }
    if (character == ' ') return space;
    if (character == '>') return greater;
    if (character == ':') return colon;
    if (character == '-') return minus;
    if (character == '.') return period;
    if (character == '/') return slash;
    return question;
}

status_e st7789::pixel(uint16_t x, uint16_t y, color_t color) {
    const auto status = window({x, y, 1, 1});
    if (status != status_e::ok) return status;
    const uint8_t bytes[] = {uint8_t(color >> 8), uint8_t(color)};
    return write_pixels(bytes);
}

status_e st7789::line(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, color_t color) {
    if (!ready_ || x0 >= panel_.width || x1 >= panel_.width ||
        y0 >= panel_.height || y1 >= panel_.height) return status_e::invalid;
    const int dx = std::abs(int(x1) - x0);
    const int dy = std::abs(int(y1) - y0);
    if (y0 == y1) return fill({std::min(x0, x1), y0, uint16_t(dx + 1), 1}, color);
    if (x0 == x1) return fill({x0, std::min(y0, y1), 1, uint16_t(dy + 1)}, color);

    int error = dx - dy;
    const int step_x = x0 < x1 ? 1 : -1;
    const int step_y = y0 < y1 ? 1 : -1;
    for (;;) {
        const auto status = pixel(x0, y0, color);
        if (status != status_e::ok || (x0 == x1 && y0 == y1)) return status;
        const int twice = 2 * error;
        if (twice > -dy) {
            error -= dy;
            x0 += step_x;
        }
        if (twice < dx) {
            error += dx;
            y0 += step_y;
        }
    }
}

status_e st7789::rect(rect_t area, color_t color) {
    if (!contains(area)) return status_e::invalid;
    if (area.width == 1 || area.height == 1) return fill(area, color);
    const auto [x, y, w, h] = area;
    auto status = fill({x, y, w, 1}, color);
    if (status != status_e::ok) return status;
    status = fill({x, uint16_t(y + h - 1), w, 1}, color);
    if (status != status_e::ok || h == 2) return status;
    status = fill({x, uint16_t(y + 1), 1, uint16_t(h - 2)}, color);
    if (status != status_e::ok) return status;
    return fill({uint16_t(x + w - 1), uint16_t(y + 1), 1, uint16_t(h - 2)}, color);
}

status_e st7789::circle(uint16_t cx, uint16_t cy, uint16_t radius, color_t color) {
    if (!ready_ || cx < radius || cy < radius ||
        cx + radius >= panel_.width || cy + radius >= panel_.height) return status_e::invalid;
    int x = radius;
    int y = 0;
    int error = 1 - x;
    while (x >= y) {
        const int points[][2] = {
            {x, y}, {y, x}, {-y, x}, {-x, y},
            {-x, -y}, {-y, -x}, {y, -x}, {x, -y}
        };
        for (const auto& [sx, sy] : points) {
            const auto status = pixel(cx + sx, cy + sy, color);
            if (status != status_e::ok) return status;
        }
        ++y;
        if (error < 0) {
            error += 2 * y + 1;
        } else {
            --x;
            error += 2 * (y - x) + 1;
        }
    }
    return status_e::ok;
}

static color_t glyph_color(const uint8_t *glyph, unsigned x, unsigned y, color_t fg,
                           color_t bg, color_t shadow, font_e font) {
    if (x < glyph_width && y < glyph_height && (glyph[x] & (1U << y))) return fg;
    if (font == font_e::classic || x == 0) return bg;
    const uint8_t left = glyph[x - 1];
    if (left & (1U << y)) return fg;
    return y && (left & (1U << (y - 1))) ? shadow : bg;
}

status_e st7789::text(uint16_t x, uint16_t y, std::string_view chars, color_t fg,
                      color_t bg, uint8_t scale, font_e font) {
    if (!ready_ || x >= panel_.width || y >= panel_.height || scale == 0 || scale > 3 ||
        (font != font_e::classic && font != font_e::block)) return status_e::invalid;
    const uint16_t cell_width = (glyph_width + 1) * scale;
    const uint16_t height = (glyph_height + 1) * scale;
    if (y + height > panel_.height || chars.size() > size_t(panel_.width - x) / cell_width)
        return status_e::invalid;
    if (chars.empty()) return status_e::ok;

    auto status = window({x, y, uint16_t(chars.size() * cell_width), height});
    if (status != status_e::ok) return status;
    uint8_t buf[chunk_size];
    size_t len = 0;
    const color_t shadow = (fg >> 1U) & 0x7BEFU;
    for (unsigned row = 0; row < height; ++row) {
        const unsigned glyph_y = row / scale;
        for (char ch : chars) {
            const auto *glyph = glyph_for(ch);
            for (unsigned col = 0; col < cell_width; ++col) {
                const auto color = glyph_color(glyph, col / scale, glyph_y, fg, bg, shadow, font);
                buf[len++] = uint8_t(color >> 8);
                buf[len++] = uint8_t(color);
                if (len == sizeof(buf)) {
                    status = write_pixels(buf);
                    if (status != status_e::ok) return status;
                    len = 0;
                }
            }
        }
    }
    return len ? write_pixels({buf, len}) : status_e::ok;
}

}
