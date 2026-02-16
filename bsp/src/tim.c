//
// Created by fish on 2025/9/22.
//

#include "bsp/tim.h"

#include "math.h"

/*
 *  For STM32H723VGT6
 */

#define RCC_APB1_CLOCK_FREQ_MHZ 240
#define RCC_APB2_CLOCK_FREQ_MHZ 240

void bsp_tim_set(TIM_HandleTypeDef *device, uint32_t period, uint32_t prescaler) {
    HAL_TIM_Base_DeInit(device);
    device->Init.Period    = period;
    device->Init.Prescaler = prescaler;
    HAL_TIM_Base_Init(device);
}

// usage: bsp_tim_config(&htim1, 1000);
void bsp_tim_config(TIM_HandleTypeDef *device, float p) {
    double t = 1e6 / p, period_limit = 0;
    if (device->Instance == TIM1 || device->Instance == TIM8 || device->Instance == TIM15 || device->Instance == TIM16 || device->Instance == TIM17) {
        t *= RCC_APB2_CLOCK_FREQ_MHZ;
    } else {
        t *= RCC_APB1_CLOCK_FREQ_MHZ;
    }
    if (device->Instance == TIM2 || device->Instance == TIM5 || device->Instance == TIM23 || device->Instance == TIM24) {
        period_limit = (1ll << 32);
    } else {
        period_limit = (1ll << 16);
    }
    double k = ceil(t / period_limit);
    bsp_tim_set(device, (uint32_t)(t / k) - 1, (uint32_t)k - 1);
}

// 0 <= blank <= 1
// usage: bsp_tim_set_duty(&htim1, TIM_CHANNEL_1, 0.5);
void bsp_tim_set_duty(TIM_HandleTypeDef *device, uint32_t channel, float duty) {
    BSP_ASSERT(0 <= duty && duty <= 1);
    __HAL_TIM_SetCompare(device, channel, (uint32_t)((device->Init.Period + 1) * duty));
}

void bsp_tim_pwm_enable(TIM_HandleTypeDef *device, uint32_t channel) {
    HAL_TIM_PWM_Start(device, channel);
}

void bsp_tim_pwm_disable(TIM_HandleTypeDef *device, uint32_t channel) {
    HAL_TIM_PWM_Stop(device, channel);
}

void bsp_tim_set_callback(TIM_HandleTypeDef *device, void (*callback)(TIM_HandleTypeDef *htim)) {
    HAL_TIM_Base_Stop_IT(device);
    HAL_TIM_RegisterCallback(device, HAL_TIM_PERIOD_ELAPSED_CB_ID, callback);
    HAL_TIM_Base_Start_IT(device);
}