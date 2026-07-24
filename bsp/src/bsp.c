//
// Created by fish on 2025/9/24.
//

#include "bsp/bsp.h"

#include "bsp/flash.h"
#include "bsp/can.h"
#include "bsp/def.h"
#include "bsp/imu.h"
#include "bsp/led.h"
#include "bsp/buzzer.h"

#include "task.h"
#include "iwdg.h"
#include "bsp/usb.h"
#include "bsp/adc.h"

void bsp_iwdg_refresh() {
    HAL_IWDG_Refresh(&hiwdg1);
}

static const char *volatile assert_expr;
static const char *volatile assert_file;
static volatile int assert_line;

void bsp_assert_failed(const char *expr, const char *file, int line) {
    assert_expr = expr;
    assert_file = file;
    assert_line = line;

    __disable_irq();
    bsp_led_set(255, 0, 0);

    if (CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk) {
        __asm volatile("bkpt 0");
    }

    for (;;) {
        if (hiwdg1.Instance != NULL) hiwdg1.Instance->KR = 0x0000AAAAU;
        __asm volatile("nop");
    }
}

uint32_t bsp_hw_init() {
    uint32_t failed = 0;
    if (bsp_led_init() != BSP_STATUS_OK) failed |= BSP_HW_LED;
    bsp_usb_init();
    if (bsp_can_init(E_CAN_1) != BSP_STATUS_OK) failed |= BSP_HW_CAN_1;
    if (bsp_can_init(E_CAN_2) != BSP_STATUS_OK) failed |= BSP_HW_CAN_2;
    if (bsp_can_init(E_CAN_3) != BSP_STATUS_OK) failed |= BSP_HW_CAN_3;
    if (bsp_imu_init() != BSP_STATUS_OK) failed |= BSP_HW_IMU;
    if (bsp_adc_init() != BSP_STATUS_OK) failed |= BSP_HW_ADC;
    if (!bsp_flash_init()) failed |= BSP_HW_FLASH;
    if (bsp_buzzer_init() != BSP_STATUS_OK) failed |= BSP_HW_BUZZER;
    return failed;
}
