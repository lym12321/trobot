//
// Created by fish on 2025/9/13.
//

#include "bsp/bsp.h"
#include "bsp/led.h"
#include "bsp/time.h"

extern "C" [[noreturn]] void app_entrance(void *args) {
    bsp_hw_init();
    for (;;) {
        bsp_led_set_hsv(static_cast<float>(bsp_time_get_ms() % 3000) / 3000.0f, 1.0f, 0.3f);
        bsp_time_delay(5);
    }
}
