//
// Created by fish on 2025/9/26.
//

#include "bsp/led.h"

#include "math.h"
#include "tim.h"

#define WS2812_HIGH 168
#define WS2812_LOW  84

static uint16_t buf[24+48];

void bsp_led_init() {
    for (uint8_t i = 0; i < 24+48; i++) buf[i] = 0;
    HAL_TIMEx_PWMN_Start_DMA(&htim8, TIM_CHANNEL_1, (uint32_t *) buf, 24+48);
}

void bsp_led_set(uint8_t r, uint8_t g, uint8_t b) {
    for (uint8_t i = 0; i < 8; i++) {
        buf[7 - i]  = ((g >> i) & 1) ? WS2812_HIGH : WS2812_LOW;
        buf[15 - i] = ((r >> i) & 1) ? WS2812_HIGH : WS2812_LOW;
        buf[23 - i] = ((b >> i) & 1) ? WS2812_HIGH : WS2812_LOW;
    }
}

void bsp_led_set_hsv(float h, float s, float v) {
    h = fmodf(h, 1.0f) * 6.0f;

    float f = h - floorf(h), p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));

    float r,g,b;
    switch((int) (floorf(h)) % 6){
        case 0: r=v; g=t; b=p; break;
        case 1: r=q; g=v; b=p; break;
        case 2: r=p; g=v; b=t; break;
        case 3: r=p; g=q; b=v; break;
        case 4: r=t; g=p; b=v; break;
        default:r=v; g=p; b=q; break;
    }

    bsp_led_set((uint8_t) (r*255+0.5f), (uint8_t) (g*255+0.5f), (uint8_t) (b*255+0.5f));
}