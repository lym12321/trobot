//
// Created by fish on 2026/1/6.
//

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint8_t bsp_flash_init();
void bsp_flash_read(const char *s, void *buf, size_t len);
uint8_t bsp_flash_write(const char *s, void *buf, size_t len);

#ifdef __cplusplus
}
#endif