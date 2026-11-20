//
// Created by fish on 2025/9/26.
//

#pragma once

#include "bsp/def.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 生成一个 [l, r] 区间内的随机数
 * @param l 区间左端点
 * @param r 区间右端点
 * @param result 输出的随机数
 * @return BSP_STATUS_OK 或硬件/参数错误状态
 */
bsp_status_t bsp_rng_random(int l, int r, int *result);

#ifdef __cplusplus
}
#endif

