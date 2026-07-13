//
// Created by fish on 2026/1/6.
//

#include "bsp/buzzer.h"

#include "tim.h"
#include "bsp/tim.h"
#include "bsp/time.h"

bsp_status_t bsp_buzzer_init() {
    return bsp_status_from_hal(HAL_TIM_PWM_Start(&htim12, TIM_CHANNEL_2));
}

bsp_status_t bsp_buzzer_alarm(float freq, float blank) {
    bsp_status_t status = bsp_tim_config(&htim12, freq);
    if (status == BSP_STATUS_OK) status = bsp_tim_set_duty(&htim12, TIM_CHANNEL_2, blank);
    return status;
}

bsp_status_t bsp_buzzer_quiet() {
    return bsp_tim_set_duty(&htim12, TIM_CHANNEL_2, 0);
}

bsp_status_t bsp_buzzer_flash(float freq, float blank, uint32_t duration) {
    const bsp_status_t status = bsp_buzzer_alarm(freq, blank);
    if (status != BSP_STATUS_OK) return status;
    bsp_time_delay(duration);
    return bsp_buzzer_quiet();
}
