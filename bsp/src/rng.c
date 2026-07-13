//
// Created by fish on 2025/9/26.
//

#include "bsp/rng.h"

#include "rng.h"

bsp_status_t bsp_rng_random(int l, int r, int *result) {
    if (l > r || result == NULL) return BSP_STATUS_ERROR;
    uint32_t rng;
    const bsp_status_t status = bsp_status_from_hal(HAL_RNG_GenerateRandomNumber(&hrng, &rng));
    if (status != BSP_STATUS_OK) return status;
    const uint64_t span = (uint64_t) ((int64_t) r - l) + 1;
    *result = (int) ((int64_t) l + rng % span);
    return BSP_STATUS_OK;
}
