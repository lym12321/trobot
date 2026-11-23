#include "bsp/display.h"

#include "bsp/time.h"
#include "spi.h"

void bsp_display_reset(void) {
    HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_SET);
    CLEAR_BIT(hspi1.Instance->CR1, SPI_CR1_SPE);
    MODIFY_REG(hspi1.Instance->CR2, SPI_CR2_TSIZE, 0);
    hspi1.Instance->IFCR = SPI_IFCR_EOTC | SPI_IFCR_TXTFC | SPI_IFCR_UDRC |
        SPI_IFCR_OVRC | SPI_IFCR_CRCEC | SPI_IFCR_TIFREC | SPI_IFCR_MODFC | SPI_IFCR_SUSPC;
    SET_BIT(hspi1.Instance->CR1, SPI_CR1_SPE);
    SET_BIT(hspi1.Instance->CR1, SPI_CR1_CSTART);

    HAL_GPIO_WritePin(LCD_RES_GPIO_Port, LCD_RES_Pin, GPIO_PIN_RESET);
    bsp_time_delay(100);
    HAL_GPIO_WritePin(LCD_RES_GPIO_Port, LCD_RES_Pin, GPIO_PIN_SET);
    bsp_time_delay(100);
}

void bsp_display_backlight(bool enabled) {
    HAL_GPIO_WritePin(LCD_BLK_GPIO_Port, LCD_BLK_Pin, enabled ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static bsp_status_t send_bytes(const uint8_t *bytes, size_t len) {
    const uint32_t start = bsp_time_get_ms();
    while (len--) {
        while (!(hspi1.Instance->SR & SPI_SR_TXP)) {
            if (bsp_time_get_ms() - start >= 10) return BSP_STATUS_TIMEOUT;
        }
        *(__IO uint8_t *) &hspi1.Instance->TXDR = *bytes++;
    }
    while (!(hspi1.Instance->SR & SPI_SR_TXC)) {
        if (bsp_time_get_ms() - start >= 10) return BSP_STATUS_TIMEOUT;
    }
    return BSP_STATUS_OK;
}

bsp_status_t bsp_display_command(uint8_t cmd, const uint8_t *args, size_t len) {
    if (len && args == NULL) return BSP_STATUS_ERROR;
    HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_RESET);
    bsp_status_t status = send_bytes(&cmd, 1);
    if (status == BSP_STATUS_OK && len) {
        HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, GPIO_PIN_SET);
        status = send_bytes(args, len);
    }
    HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_SET);
    return status;
}

bsp_status_t bsp_display_data(const uint8_t *bytes, size_t len) {
    if (bytes == NULL || len == 0) return BSP_STATUS_ERROR;
    HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_RESET);
    const bsp_status_t status = send_bytes(bytes, len);
    HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_SET);
    return status;
}
