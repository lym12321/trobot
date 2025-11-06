//
// Created by fish on 2025/9/13.
//

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t bsp_time_get_ms();
uint64_t bsp_time_get_us();

void bsp_time_delay(uint32_t ms);
void bsp_time_delay_us(uint32_t us);

#ifdef __cplusplus
}
#endif