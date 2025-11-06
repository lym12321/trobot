//
// Created by fish on 2025/9/19.
//

#pragma once

#include "def.h"

typedef enum { IO_SET, IO_RESET } bsp_io_state_e;

void bsp_io_set(bsp_io_t io);

void bsp_io_reset(bsp_io_t io);

void bsp_io_toggle(bsp_io_t io);

bsp_io_state_e bsp_io_read(bsp_io_t io);