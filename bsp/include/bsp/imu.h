//
// Created by fish on 2025/9/26.
//

#pragma once

#include "bsp/def.h"

typedef struct {
    float accel[3];  // m/s^2, sensor axes
    float gyro[3];   // rad/s, sensor axes
    float temp;      // deg C
} bsp_imu_data_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 初始化板载 bmi088
 */
bsp_status_t bsp_imu_init();

/**
 * 读取 bmi088 数据
 * @param data 输出数据；加速度单位为 m/s^2，陀螺仪单位为 rad/s，温度单位为 deg C
 */
bsp_status_t bsp_imu_read(bsp_imu_data_t *data);

#ifdef __cplusplus
}
#endif
