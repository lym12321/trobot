//
// Created by fish on 2025/9/25.
//

#pragma once

#include <bsp/def.h>
#include <spi.h>

void bsp_spi_send(SPI_HandleTypeDef *device, const uint8_t *data, uint16_t len);
void bsp_spi_recv(SPI_HandleTypeDef *device, uint8_t *data, uint16_t len);
void bsp_spi_send_recv(SPI_HandleTypeDef *device, const uint8_t *data, uint8_t *recv, uint16_t len);