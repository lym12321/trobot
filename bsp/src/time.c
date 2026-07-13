//
// Created by fish on 2025/9/13.
//

#include "bsp/time.h"

#include "main.h"
#include "FreeRTOS.h"
#include "cmsis_os2.h"
#include "task.h"

uint32_t bsp_time_get_ms() {
    return xTaskGetTickCount();
}

uint64_t bsp_time_get_us() {
    uint32_t ms_old = xTaskGetTickCount();
    uint32_t tick_val_old = SysTick->VAL;
    uint32_t ms_now = xTaskGetTickCount();
    uint32_t tick_val_now = SysTick->VAL;
    return ms_old == ms_now ?
        (uint64_t) ms_now * 1000 + 1000 - tick_val_old * 1000 / (SysTick->LOAD + 1):
        (uint64_t) ms_now * 1000 + 1000 - tick_val_now * 1000 / (SysTick->LOAD + 1);
}

void bsp_time_delay(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}

void bsp_time_delay_us(uint32_t us) {
    uint64_t cur = bsp_time_get_us();
    while (bsp_time_get_us() - cur < us) __NOP();
}

void bsp_dwt_init(void) {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

uint32_t bsp_dwt_get(void) {
    return DWT->CYCCNT;
}
