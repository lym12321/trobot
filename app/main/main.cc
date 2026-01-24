//
// Created by fish on 2025/9/13.
//


#include "bsp/bsp.h"
#include "bsp/led.h"
#include "bsp/time.h"

#include "bsp/buzzer.h"
#include "ins/ins.h"
#include "rc/dr16.h"
#include "utils/os.h"

extern "C" [[noreturn]] void app_entrance(void *args) {
    bsp_hw_init();

    bsp_led_set(0, 50, 0);
    bsp_buzzer_flash(4500, 0.2f, 100);

    bsp_time_delay(100);

    // logger::init(E_UART_1, logger::INFO);
    // terminal::init(E_UART_1, 921600);

    ins::init();
    while (!ins::inited) bsp_iwdg_refresh(), bsp_time_delay(10);

    rc::dr16::init(E_UART_5);

    bsp_buzzer_flash(4500, 0.2f, 75);
    bsp_time_delay(50);
    bsp_buzzer_flash(4500, 0.2f, 75);

    for (;;) {
        bsp_led_set_hsv(static_cast<float>(bsp_time_get_ms() % 3000) / 3000.0f, 1.0f, 0.3f);
        bsp_iwdg_refresh();
        os::task::sleep(5);
    }
}
