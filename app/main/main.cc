//
// Created by fish on 2025/9/13.
//

#include "bsp/bsp.h"
#include "bsp/led.h"
#include "bsp/time.h"

#include "bsp/buzzer.h"
#include "rc/dr16.h"
#include "utils/os.h"

extern void example_task(void *args);

extern "C" [[noreturn]] void app_entrance(void *args) {
    bsp_hw_init();

    bsp_led_set(0, 50, 0);
    bsp_buzzer_flash(4500, 0.2f, 100);
    bsp_time_delay(100);

    // Init Basic Components

    // logger::init(E_UART_1, logger::INFO);
    // terminal::init(E_UART_1, 921600);
    rc::dr16::init(E_UART_5);

    bsp_buzzer_flash(4500, 0.2f, 75);
    bsp_time_delay(50);
    bsp_buzzer_flash(4500, 0.2f, 75);

    // Init Application Tasks
    os::task::static_create(example_task, nullptr, "example_task", 512, os::task::Priority::HIGH);

    for (;;) {
        bsp_led_set_hsv(static_cast<float>(bsp_time_get_ms() % 3000) / 3000.0f, 1.0f, 0.3f);
        bsp_iwdg_refresh();
        os::task::sleep(5);
    }
}
