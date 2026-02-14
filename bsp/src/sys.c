//
// Created by fish on 2026/1/8.
//

#include "bsp/sys.h"

#include "main.h"

#include "FreeRTOS.h"
#include "task.h"

uint8_t bsp_sys_in_isr() {
    uint32_t result;
    __asm__ volatile("MRS %0, ipsr" : "=r"(result));
    return result;
}

void bsp_sys_reset() {
    __set_FAULTMASK(1);
    NVIC_SystemReset();
}

UBaseType_t bsp_sys_enter_critical() {
    if (bsp_sys_in_isr()) return taskENTER_CRITICAL_FROM_ISR();
    taskENTER_CRITICAL();
    return 0;
}

void bsp_sys_exit_critical(UBaseType_t state) {
    if (bsp_sys_in_isr()) taskEXIT_CRITICAL_FROM_ISR(state);
    else taskEXIT_CRITICAL();
}