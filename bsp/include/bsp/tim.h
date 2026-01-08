//
// Created by fish on 2025/9/22.

#pragma once

#include "bsp/def.h"

void bsp_tim_set(bsp_timer_handle_t *device, uint32_t period, uint32_t prescaler);

void bsp_tim_config(bsp_timer_handle_t *device, float p);

void bsp_tim_set_duty(bsp_timer_handle_t *device, uint32_t channel, float duty);

void bsp_tim_pwm_enable(bsp_timer_handle_t *device, uint32_t channel);

void bsp_tim_pwm_disable(bsp_timer_handle_t *device, uint32_t channel);