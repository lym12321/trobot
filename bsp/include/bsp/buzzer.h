//
// Created by fish on 2026/1/6.
//

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void bsp_buzzer_init();
void bsp_buzzer_alarm(float freq, float blank);
void bsp_buzzer_quiet();
void bsp_buzzer_flash(float freq, float blank, uint32_t duration);

#ifdef __cplusplus
}
#endif