//
// Created by fish on 2025/9/26.
//

#include "bsp/def.h"

#ifdef __cplusplus
extern "C" {
#endif

void bsp_led_init();
void bsp_led_set(uint8_t r, uint8_t g, uint8_t b);
void bsp_led_set_hsv(float h, float s, float v);

#ifdef __cplusplus
}
#endif