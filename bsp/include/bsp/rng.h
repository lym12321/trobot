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
 * @return [l, r] 区间内的随机数
 */
bsp_status_t bsp_rng_random(int l, int r, int *result);

#ifdef __cplusplus
}
#endif

