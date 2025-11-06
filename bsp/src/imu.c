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

/**
 * @brief 将原始值转为以 g 值表示的加速度
 * @{
 */
#define BMI088_ACCEL_3G_SEN  (3.f / 32768)
#define BMI088_ACCEL_6G_SEN  (6.f / 32768)
#define BMI088_ACCEL_12G_SEN (12.f / 32768)
#define BMI088_ACCEL_24G_SEN (24.f / 32768)
/** @} */

/**
 * @brief 将原始值转为 deg/s 为单位的角速度
 * @{
 */
#define BMI088_GYRO_2000_SEN (2000.f / 32768)
#define BMI088_GYRO_1000_SEN (1000.f / 32768)
#define BMI088_GYRO_500_SEN  (500.f / 32768)
#define BMI088_GYRO_250_SEN  (250.f / 32768)
#define BMI088_GYRO_125_SEN  (125.f / 32768)
/** @} */

#define BMI088_TEMP_FACTOR 0.125f
#define BMI088_TEMP_OFFSET 23.0f

static uint8_t rx_buf[10];

static void write_reg(uint8_t addr, uint8_t val) {
    uint8_t buf[2] = { addr & 0x7f, val };
    bsp_spi_send(&SPI_PORT, buf, 2);
    bsp_time_delay_us(200);
}

static void read_reg(uint8_t addr, size_t len) {
    addr |= 0x80;
    bsp_spi_send(&SPI_PORT, &addr, 1);
    bsp_spi_recv(&SPI_PORT, rx_buf, len);
    bsp_time_delay_us(50);
}

static void accel_write(uint8_t addr, uint8_t val) {
    ACCEL_CS_LOW();
    write_reg(addr, val);
    ACCEL_CS_HIGH();
}

static void gyro_write(uint8_t addr, uint8_t val) {
    GYRO_CS_LOW();
    write_reg(addr, val);
    GYRO_CS_HIGH();
}

static void accel_read(uint8_t addr, uint8_t *buf, uint8_t len) {
    ACCEL_CS_LOW();
    read_reg(addr, len + 1);
    memcpy(buf, rx_buf + 1, len);
    ACCEL_CS_HIGH();
}

static void gyro_read(uint8_t addr, uint8_t *buf, uint8_t len) {
    GYRO_CS_LOW();
    read_reg(addr, len);
    memcpy(buf, rx_buf, len);
    GYRO_CS_HIGH();
}

void bsp_imu_init() {
    accel_write(BMI088_ACC_SOFTRESET, BMI088_ACC_SOFTRESET_VALUE);
    bsp_time_delay(50);
    gyro_write(BMI088_GYRO_SOFTRESET, BMI088_GYRO_SOFTRESET_VALUE);
    bsp_time_delay(50);

    uint8_t resp;
    accel_read(BMI088_ACC_CHIP_ID, &resp, 1);

    accel_read(BMI088_ACC_CHIP_ID, &resp, 1);
    BSP_ASSERT(resp == BMI088_ACC_CHIP_ID_VALUE);

    gyro_read(BMI088_GYRO_CHIP_ID, &resp, 1);
    BSP_ASSERT(resp == BMI088_GYRO_CHIP_ID_VALUE);

    accel_write(BMI088_ACC_PWR_CONF, 0x00);                 // Normal 模式
    accel_write(BMI088_ACC_PWR_CTRL, 0x04);                 // 打开三轴加速度计

    accel_write(BMI088_ACC_RANGE, BMI088_ACC_RANGE_3G);     // 加速度计量程
    gyro_write(BMI088_GYRO_RANGE, BMI088_GYRO_2000);        // 陀螺仪量程

    accel_write(BMI088_ACC_CONF, BMI088_ACC_NORMAL | BMI088_ACC_800_HZ | BMI088_ACC_CONF_MUST_Set);
    gyro_write(BMI088_GYRO_BANDWIDTH, BMI088_GYRO_1000_116_HZ | BMI088_GYRO_BANDWIDTH_MUST_Set);

}

bsp_imu_data_t bsp_imu_read() {
    bsp_imu_data_t ret = { };

    union {
        uint8_t buf[6];
        int16_t raw[3];
    } dat;

    accel_read(BMI088_ACCEL_XOUT_L, dat.buf, 6);
    for (uint8_t i = 0; i < 3; i++) {
        ret.accel[i] = (float) dat.raw[i] * BMI088_ACCEL_3G_SEN;
        // ret.accel[i] *= -1;
    }

    gyro_read(BMI088_GYRO_X_L, dat.buf, 6);
    for (uint8_t i = 0; i < 3; i++) {
        ret.gyro[i] = (float) dat.raw[i] * BMI088_GYRO_2000_SEN;
        ret.gyro[i] *= M_PI / 180.f;
    }

    accel_read(BMI088_TEMP_M, dat.buf, 2);
    int16_t _temp = (int16_t) ((dat.buf[0] << 3) | (dat.buf[1] >> 5));
    if (_temp > 1023) _temp -= 2048;
    ret.temp = (float) _temp * BMI088_TEMP_FACTOR + BMI088_TEMP_OFFSET;

    return ret;
}
