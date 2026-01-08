//
// Created by fish on 2026/1/6.
//

#include "bsp/buzzer.h"

#include "tim.h"
#include "bsp/tim.h"
#include "bsp/time.h"

void bsp_buzzer_init() {
    HAL_TIM_PWM_Start(&htim12, TIM_CHANNEL_2);
}

void bsp_buzzer_alarm(float freq, float blank) {
    bsp_tim_config(&htim12, freq);
    bsp_tim_set_duty(&htim12, TIM_CHANNEL_2, blank);
}

void bsp_buzzer_quiet() {
    bsp_tim_set_duty(&htim12, TIM_CHANNEL_2, 0);
}

void bsp_buzzer_flash(float freq, float blank, uint32_t duration) {
    bsp_buzzer_alarm(freq, blank);
    bsp_time_delay(duration);
    bsp_buzzer_quiet();
}