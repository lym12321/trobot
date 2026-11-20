//
// Created by fish on 2025/9/13.
//

#include "bsp/bsp.h"
#include "bsp/imu.h"
#include "bsp/led.h"
#include "bsp/time.h"

#include "bsp/buzzer.h"
#include "ins/ins.h"
#include "music/music.h"
#include "utils/logger.h"
#include "utils/os.h"
#include "utils/terminal.h"

extern void example_task(void *args);

namespace {
constexpr uint32_t imu_retry_limit = 3;
constexpr uint32_t imu_retry_delay_ms = 500;
}

extern "C" [[noreturn]] void app_entrance(void *args) {
    uint32_t hw_failed = bsp_hw_init();

    if ((hw_failed & BSP_HW_IMU) != 0) {
        for (uint32_t attempt = 0; attempt < imu_retry_limit; ++attempt) {
            if (bsp_imu_init() == BSP_STATUS_OK) {
                hw_failed &= ~BSP_HW_IMU;
                break;
            }
            bsp_led_set(50, 0, 0);
            if (attempt + 1 < imu_retry_limit) {
                bsp_iwdg_refresh();
                os::task::sleep(imu_retry_delay_ms);
            }
        }
        if ((hw_failed & BSP_HW_IMU) != 0) {
            for (;;) os::task::sleep(100);
        }
    }

    const bool startup_ok = hw_failed == 0;
    bsp_led_set(startup_ok ? 0 : 50, startup_ok ? 50 : 0, 0);
    bsp_buzzer_flash(4500, 0.2f, 100);
    bsp_time_delay(100);

    // Init Basic Components

    music::init();
    logger::init_flash(logger::WARNING);
    // logger::init(E_UART_1, logger::INFO);
    // terminal::init(E_UART_1);

    ins::init();
    while (!ins::ready()) {
        os::task::sleep(5);
        bsp_iwdg_refresh();
    }

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
        if (startup_ok) {
            bsp_led_set_hsv(static_cast<float>(bsp_time_get_ms() % 3000) / 3000.0f, 1.0f, 0.3f);
        }
        bsp_iwdg_refresh();
        os::task::sleep(5);
    }
}
