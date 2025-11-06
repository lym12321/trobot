//
// Created by fish on 2025/9/26.
//

#pragma once

typedef struct {
    float accel[3], gyro[3], temp;
} bsp_imu_data_t;

#ifdef __cplusplus
extern "C" {
#endif

void bsp_imu_init();
bsp_imu_data_t bsp_imu_read();

#ifdef __cplusplus
}
#endif
