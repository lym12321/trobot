//
// Created by fish on 2026/1/8.
//

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint8_t bsp_sys_in_isr();
void bsp_sys_reset();
unsigned long bsp_sys_enter_critical();
void bsp_sys_exit_critical(unsigned long state);

#ifdef __cplusplus
}
#endif