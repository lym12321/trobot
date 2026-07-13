//
// Created by fish on 2025/9/26.
//

#include "bsp/imu.h"

#include <string.h>

#include "bsp/spi.h"
#include "bsp/time.h"
#include "bsp/bmi088_reg.h"

#include "gpio.h"

#define SPI_PORT hspi2

#define ACCEL_CS_LOW()                                                      \
    HAL_GPIO_WritePin(ACC_CS_GPIO_Port, ACC_CS_Pin, GPIO_PIN_RESET)
#define ACCEL_CS_HIGH()                                                     \
    HAL_GPIO_WritePin(ACC_CS_GPIO_Port, ACC_CS_Pin, GPIO_PIN_SET)

#define GYRO_CS_LOW()                                                       \
    HAL_GPIO_WritePin(GYRO_CS_GPIO_Port, GYRO_CS_Pin, GPIO_PIN_RESET)
#define GYRO_CS_HIGH()                                                      \
    HAL_GPIO_WritePin(GYRO_CS_GPIO_Port, GYRO_CS_Pin, GPIO_PIN_SET)


const float g = 9.80665f;

/**
 * @brief 将原始值转为以 m/s^2 为单位的加速度
 * @{
 */
#define BMI088_ACCEL_3G_SEN  (g * 3.f / 32768)
#define BMI088_ACCEL_6G_SEN  (g * 6.f / 32768)
#define BMI088_ACCEL_12G_SEN (g * 12.f / 32768)
#define BMI088_ACCEL_24G_SEN (g * 24.f / 32768)
/** @} */

/**
 * @brief 将原始值转为以 rad/s 为单位的角速度
 * @{
 */
#define BMI088_GYRO_2000_SEN ((float) M_PI / 180.f * 2000.f / 32768)
#define BMI088_GYRO_1000_SEN ((float) M_PI / 180.f * 1000.f / 32768)
#define BMI088_GYRO_500_SEN  ((float) M_PI / 180.f * 500.f / 32768)
#define BMI088_GYRO_250_SEN  ((float) M_PI / 180.f * 250.f / 32768)
#define BMI088_GYRO_125_SEN  ((float) M_PI / 180.f * 125.f / 32768)
/** @} */

#define BMI088_TEMP_FACTOR 0.125f
#define BMI088_TEMP_OFFSET 23.0f

static uint8_t rx_buf[10];

static bsp_status_t write_reg(uint8_t addr, uint8_t val) {
    uint8_t buf[2] = { addr & 0x7f, val };
    const bsp_status_t status = bsp_spi_send(&SPI_PORT, buf, 2);
    bsp_time_delay_us(200);
    return status;
}

static bsp_status_t read_reg(uint8_t addr, size_t len) {
    addr |= 0x80;
    bsp_status_t status = bsp_spi_send(&SPI_PORT, &addr, 1);
    if (status == BSP_STATUS_OK) status = bsp_spi_recv(&SPI_PORT, rx_buf, len);
    bsp_time_delay_us(50);
    return status;
}

static bsp_status_t accel_write(uint8_t addr, uint8_t val) {
    ACCEL_CS_LOW();
    const bsp_status_t status = write_reg(addr, val);
    ACCEL_CS_HIGH();
    return status;
}

static bsp_status_t gyro_write(uint8_t addr, uint8_t val) {
    GYRO_CS_LOW();
    const bsp_status_t status = write_reg(addr, val);
    GYRO_CS_HIGH();
    return status;
}

static bsp_status_t accel_read(uint8_t addr, uint8_t *buf, uint8_t len) {
    ACCEL_CS_LOW();
    const bsp_status_t status = read_reg(addr, len + 1);
    if (status == BSP_STATUS_OK) memcpy(buf, rx_buf + 1, len);
    ACCEL_CS_HIGH();
    return status;
}

static bsp_status_t gyro_read(uint8_t addr, uint8_t *buf, uint8_t len) {
    GYRO_CS_LOW();
    const bsp_status_t status = read_reg(addr, len);
    if (status == BSP_STATUS_OK) memcpy(buf, rx_buf, len);
    GYRO_CS_HIGH();
    return status;
}

bsp_status_t bsp_imu_init() {
    bsp_status_t status = accel_write(BMI088_ACC_SOFTRESET, BMI088_ACC_SOFTRESET_VALUE);
    if (status != BSP_STATUS_OK) return status;
    bsp_time_delay(50);
    status = gyro_write(BMI088_GYRO_SOFTRESET, BMI088_GYRO_SOFTRESET_VALUE);
    if (status != BSP_STATUS_OK) return status;
    bsp_time_delay(50);

    uint8_t resp;
    status = accel_read(BMI088_ACC_CHIP_ID, &resp, 1);
    if (status != BSP_STATUS_OK) return status;

    status = accel_read(BMI088_ACC_CHIP_ID, &resp, 1);
    if (status != BSP_STATUS_OK) return status;
    if (resp != BMI088_ACC_CHIP_ID_VALUE) return BSP_STATUS_OFFLINE;

    status = gyro_read(BMI088_GYRO_CHIP_ID, &resp, 1);
    if (status != BSP_STATUS_OK) return status;
    if (resp != BMI088_GYRO_CHIP_ID_VALUE) return BSP_STATUS_OFFLINE;

    status = accel_write(BMI088_ACC_PWR_CONF, 0x00);                 // Normal 模式
    if (status == BSP_STATUS_OK) status = accel_write(BMI088_ACC_PWR_CTRL, 0x04); // 打开三轴加速度计

    if (status == BSP_STATUS_OK) status = accel_write(BMI088_ACC_RANGE, BMI088_ACC_RANGE_3G); // 加速度计量程
    if (status == BSP_STATUS_OK) status = gyro_write(BMI088_GYRO_RANGE, BMI088_GYRO_2000);    // 陀螺仪量程

    if (status == BSP_STATUS_OK) status = accel_write(BMI088_ACC_CONF, BMI088_ACC_NORMAL | BMI088_ACC_800_HZ | BMI088_ACC_CONF_MUST_Set);
    if (status == BSP_STATUS_OK) status = gyro_write(BMI088_GYRO_BANDWIDTH, BMI088_GYRO_1000_116_HZ | BMI088_GYRO_BANDWIDTH_MUST_Set);
    return status;
}

bsp_status_t bsp_imu_read(bsp_imu_data_t *data) {
    if (data == NULL) return BSP_STATUS_ERROR;
    bsp_imu_data_t ret = { };

    union {
        uint8_t buf[6];
        int16_t raw[3];
    } dat;

    bsp_status_t status = accel_read(BMI088_ACCEL_XOUT_L, dat.buf, 6);
    if (status != BSP_STATUS_OK) return status;
    for (uint8_t i = 0; i < 3; i++) {
        ret.accel[i] = (float) dat.raw[i] * BMI088_ACCEL_3G_SEN;
    }

    status = gyro_read(BMI088_GYRO_X_L, dat.buf, 6);
    if (status != BSP_STATUS_OK) return status;
    for (uint8_t i = 0; i < 3; i++) {
        ret.gyro[i] = (float) dat.raw[i] * BMI088_GYRO_2000_SEN;
    }

    status = accel_read(BMI088_TEMP_M, dat.buf, 2);
    if (status != BSP_STATUS_OK) return status;
    int16_t _temp = (int16_t) ((dat.buf[0] << 3) | (dat.buf[1] >> 5));
    if (_temp > 1023) _temp -= 2048;
    ret.temp = (float) _temp * BMI088_TEMP_FACTOR + BMI088_TEMP_OFFSET;

    *data = ret;
    return BSP_STATUS_OK;
}
