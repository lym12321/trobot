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
#include "utils/log_disk.h"
#include "utils/logger.h"
#include "utils/os.h"
#include "utils/terminal.h"

extern void example_task(void *args);

namespace {
constexpr uint32_t imu_retry_limit = 3;
constexpr uint32_t imu_retry_delay_ms = 500;
}

extern "C" [[noreturn]] void app_entrance(void *args) {
    if (bsp_user_key_pressed()) {
        os::task::sleep(10);
        if (bsp_user_key_pressed()) {
            const bsp_usb_config_t usb{.cdc_count = 1, .winusb = false, .disk = logger::prepare_log_disk()};
            if (bsp_usb_init(&usb) != BSP_STATUS_OK && bsp_led_init() == BSP_STATUS_OK)
                bsp_led_set(50, 0, 0);
            for (;;) {
                bsp_usb_poll();
                bsp_iwdg_refresh();
                os::task::sleep(1);
            }
        }
    }
    constexpr bsp_usb_config_t usb{
        .cdc_count = BSP_USB_CDC_COUNT, .winusb = USB_WINUSB_ENABLE != 0, .disk = nullptr
    };
    uint32_t hw_failed = bsp_hw_init(&usb);

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
    logger::init_flash(logger::WARNING);
    // logger::init(E_UART_1, logger::INFO);
    bsp_dwt_init();
    ins::init();

    bsp_led_set(hw_failed ? 50 : 0, hw_failed ? 0 : 50, 0);
    bsp_buzzer_flash(4500, 0.2f, 100);
    bsp_time_delay(100);

    // Init Basic Components

    // music::init();
    // terminal::init(E_UART_1);
    // logger::register_terminal_commands();
    // ins::register_terminal_commands();
    // music::register_terminal_commands();
    while (!ins::ready()) {
        os::task::sleep(5);
        bsp_iwdg_refresh();
    }

    bsp_buzzer_flash(4500, 0.2f, 75);
    bsp_time_delay(50);
    bsp_buzzer_flash(4500, 0.2f, 75);

    // Init Application Tasks
    // BSP_ASSERT(os::task::static_create(example_task, nullptr, "example_task", 512, os::task::Priority::HIGH));

    for (;;) {
        if (hw_failed == 0) {
            bsp_led_set_hsv(static_cast<float>(bsp_time_get_ms() % 3000) / 3000.0f, 1.0f, 0.3f);
        }
        bsp_iwdg_refresh();
        os::task::sleep(5);
    }
}
