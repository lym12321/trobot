//
// Created by fish on 2025/9/24.
//

#include "bsp/bsp.h"
#include "bsp/can.h"
#include "bsp/def.h"
#include "bsp/imu.h"
#include "bsp/led.h"

#include "task.h"
#include "iwdg.h"
#include "usb_device.h"

void bsp_iwdg_refresh() {
    HAL_IWDG_Refresh(&hiwdg1);
}

void bsp_assert_failed(const char *expr, const char *file, int line) {
    bsp_led_set(255, 0, 0);
    if (CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk)
        __asm volatile("bkpt 0");
    else {
        vTaskSuspendAll();
        for (;;) {
            bsp_iwdg_refresh();
            HAL_Delay(10);
        }
    }
}

void bsp_hw_init() {
    MX_USB_DEVICE_Init();
    bsp_can_init(E_CAN_1);
    bsp_can_init(E_CAN_2);
    bsp_can_init(E_CAN_3);
    bsp_imu_init();
}
