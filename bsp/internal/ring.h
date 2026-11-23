#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "bsp/serial.h"

typedef struct {
    uint8_t buffer[BSP_SERIAL_BUFFER_SIZE + sizeof(uint16_t)];
    uint16_t head;
    uint16_t tail;
    uint16_t used;
} bsp_frame_ring_t;

bool bsp_frame_ring_push(bsp_frame_ring_t *ring, const uint8_t *data, uint16_t length);
bool bsp_frame_ring_pop(bsp_frame_ring_t *ring, uint8_t *data, uint16_t *length);
void bsp_frame_ring_reset(bsp_frame_ring_t *ring);
