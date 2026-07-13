//
// Created by fish on 2025/9/25.
//

#include "bsp/spi.h"

bsp_status_t bsp_spi_send(SPI_HandleTypeDef *device, const uint8_t *data, uint16_t len) {
    if (device == NULL || data == NULL || len == 0) return BSP_STATUS_ERROR;
    return bsp_status_from_hal(HAL_SPI_Transmit(device, data, len, HAL_MAX_DELAY));
}

bsp_status_t bsp_spi_recv(SPI_HandleTypeDef *device, uint8_t *data, uint16_t len) {
    if (device == NULL || data == NULL || len == 0) return BSP_STATUS_ERROR;
    return bsp_status_from_hal(HAL_SPI_Receive(device, data, len, HAL_MAX_DELAY));
}

bsp_status_t bsp_spi_send_recv(SPI_HandleTypeDef *device, const uint8_t *data, uint8_t *recv, uint16_t len) {
    if (device == NULL || data == NULL || recv == NULL || len == 0) return BSP_STATUS_ERROR;
    return bsp_status_from_hal(HAL_SPI_TransmitReceive(device, data, recv, len, HAL_MAX_DELAY));
}
