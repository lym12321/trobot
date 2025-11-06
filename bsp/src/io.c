//
// Created by fish on 2025/9/19.
//

#include "bsp/io.h"

#include "stm32h7xx_hal.h"

void bsp_io_set(bsp_io_t io) {
    HAL_GPIO_WritePin(io.port, io.pin, GPIO_PIN_SET);
}

void bsp_io_reset(bsp_io_t io) {
    HAL_GPIO_WritePin(io.port, io.pin, GPIO_PIN_RESET);
}

void bsp_io_toggle(bsp_io_t io) {
    HAL_GPIO_TogglePin(io.port, io.pin);
}

bsp_io_state_e bsp_io_read(bsp_io_t io) {
    return HAL_GPIO_ReadPin(io.port, io.pin) == GPIO_PIN_SET ? IO_SET : IO_RESET;
}