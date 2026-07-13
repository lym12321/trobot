//
// Created by fish on 2025/9/13.
//

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 获取当前系统时间戳 (ms)
 * @return 当前系统时间戳 (ms)
 */
uint32_t bsp_time_get_ms();

/**
 * 获取当前系统时间戳 (us)
 * @return 当前系统时间戳 (us)
 */
uint64_t bsp_time_get_us();

/**
 * 系统级阻塞 delay (ms)
 * @param ms 延时时间 (ms)
 */
void bsp_time_delay(uint32_t ms);

/**
 * 系统级阻塞 delay (us)
 * @param us 延时时间 (us)
 */
void bsp_time_delay_us(uint32_t us);

/**
 * 启用 DWT 周期计数器 (Cortex-M3/M4/M7)
 * 需在初始化阶段调用，耗时不计入任务调度
 */
void bsp_dwt_init(void);


/**
 * 读取 DWT 周期计数器当前值
 * @return 自上次复位以来的 CPU 周期数
 */
uint32_t bsp_dwt_get(void);

#ifdef __cplusplus
}
#endif