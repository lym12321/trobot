//
// Created by fish on 2025/9/26.
//

#include "bsp/def.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 *  初始化板载 ws2812
 */
bsp_status_t bsp_led_init();

/**
 * 设置板载 ws2812 颜色 (rgb)
 * @param r 红色通道值 (0 <= r <= 255)
 * @param g 绿色通道值 (0 <= g <= 255)
 * @param b 蓝色通道值 (0 <= b <= 255)
 */
void bsp_led_set(uint8_t r, uint8_t g, uint8_t b);

/**
 * 设置板载 ws2812 颜色 (hsv)
 * @param h 色相 (0 <= h < 1)
 * @param s 饱和度 (0 <= s <= 1)
 * @param v 明度 (0 <= v <= 1)
 */
void bsp_led_set_hsv(float h, float s, float v);

#ifdef __cplusplus
}
#endif
