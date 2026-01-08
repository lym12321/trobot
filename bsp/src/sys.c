//
// Created by fish on 2026/1/8.
//

#include "bsp/sys.h"

#include "main.h"

uint8_t bsp_sys_in_isr() {
    uint32_t result;
    __asm__ volatile("MRS %0, ipsr" : "=r"(result));
    return result;
}

void bsp_sys_reset() {
    __set_FAULTMASK(1);
    NVIC_SystemReset();
}