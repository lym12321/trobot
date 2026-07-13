//
// Created by fish on 2025/9/13.
//

#include "bsp/bsp.h"
#include "bsp/led.h"
#include "bsp/time.h"

#include "bsp/buzzer.h"
#include "ins/ins.h"
#include "rc/dr16.h"
#include "rc/ht10.h"
#include "utils/os.h"
#include "utils/terminal.h"

extern void example_task(void *args);

extern "C" [[noreturn]] void app_entrance(void *args) {
    uint32_t hw_failed = bsp_hw_init();

    bsp_led_set(hw_failed == 0 ? 0 : 50, hw_failed == 0 ? 50 : 0, 0);
    bsp_buzzer_flash(4500, 0.2f, 100);
    bsp_time_delay(100);

    // Init Basic Components

    // logger::init(E_UART_1, logger::INFO);
    // terminal::init(E_UART_1, 921600);
    // rc::dr16::init(E_UART_5);
    // rc::ht10::init(E_UART_5);

    if ((hw_failed & BSP_HW_IMU) != 0) {
        while (bsp_imu_init() != BSP_STATUS_OK) {
            bsp_led_set(50, 0, 0);
            bsp_iwdg_refresh();
            os::task::sleep(500);
        }
        hw_failed &= ~BSP_HW_IMU;
        bsp_led_set(hw_failed == 0 ? 0 : 50, hw_failed == 0 ? 50 : 0, 0);
    }

    ins::init();
    while (!ins::ready()) os::task::sleep(5), bsp_iwdg_refresh();

    bsp_buzzer_flash(4500, 0.2f, 75);
    bsp_time_delay(50);
    bsp_buzzer_flash(4500, 0.2f, 75);

    // Init Application Tasks
    BSP_ASSERT(os::task::static_create(
        example_task, nullptr, "example_task", 512, os::task::Priority::HIGH
    ));

    for (;;) {
        if (hw_failed == 0) {
            bsp_led_set_hsv(static_cast<float>(bsp_time_get_ms() % 3000) / 3000.0f, 1.0f, 0.3f);
        }
        bsp_iwdg_refresh();
        os::task::sleep(5);
    }
}
