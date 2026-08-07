//
// Created by fish on 2025/9/13.
//

#include "bsp/bsp.h"
#include "bsp/led.h"
#include "bsp/time.h"

#include "bsp/buzzer.h"
#include "ins/ins.h"
#include "music/music.h"
#include "rc/dr16.h"
#include "rc/ht10.h"
#include "utils/logger.h"
#include "utils/os.h"
#include "utils/terminal.h"
#include "debug_mode.h"

extern void example_task(void *args);

[[noreturn]] static void run_debug_mode(uint32_t hw_failed) {
    const bool ready =
        (hw_failed & (BSP_HW_FLASH | BSP_HW_USB)) == 0 &&
        logger::init_flash(logger::NONE) && debug_mode::init();
    bsp_led_set(ready ? 0 : 50, 0, ready ? 50 : 0);
    debug_mode::run(ready);
}

extern "C" [[noreturn]] void app_entrance(void *args) {
    const bool debug_mode = bsp_debug_mode_requested();
    uint32_t hw_failed = debug_mode ? bsp_debug_hw_init() : bsp_hw_init();

    if (debug_mode) run_debug_mode(hw_failed);

    bsp_led_set(hw_failed == 0 ? 0 : 50, hw_failed == 0 ? 50 : 0, 0);
    bsp_buzzer_flash(4500, 0.2f, 100);
    bsp_time_delay(100);

    // Init Basic Components

    music::init();
    logger::init_flash(logger::WARNING);
    // logger::init(E_UART_1, logger::INFO);
    // terminal::init(E_UART_1);
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

    constexpr music::tone_t ready_tone[] {
        { 4500, 75 },
        { 0, 50 },
        { 4500, 75 },
    };
    music::play_blocking(ready_tone);

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
