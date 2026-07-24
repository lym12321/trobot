//
// Created by fish on 2025/9/26.
//

#pragma once

#include "bsp/def.h"

typedef struct {
    float accel[3], gyro[3], temp;
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
 * @return 陀螺仪原始数据
 */
bsp_status_t bsp_imu_read(bsp_imu_data_t *data);

#ifdef __cplusplus
}
#endif
