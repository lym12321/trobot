#pragma once

#include <stdbool.h>

/* Read and written only by the task holding the Flash mutex. */
extern bool bsp_flash_initializing;
void bsp_flash_internal_lock(void);
void bsp_flash_internal_unlock(void);
