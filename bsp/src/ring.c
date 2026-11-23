#include "ring.h"

#include <string.h>

static void ring_write(bsp_frame_ring_t *ring, const uint8_t *data, uint16_t length) {
    const uint16_t space = sizeof(ring->buffer) - ring->head;
    const uint16_t first = space < length ? space : length;
    memcpy(ring->buffer + ring->head, data, first);
    if (first < length) memcpy(ring->buffer, data + first, length - first);
    ring->head = length >= space ? length - space : ring->head + length;
}

static void ring_read(bsp_frame_ring_t *ring, uint8_t *data, uint16_t length) {
    const uint16_t space = sizeof(ring->buffer) - ring->tail;
    const uint16_t first = space < length ? space : length;
    memcpy(data, ring->buffer + ring->tail, first);
    if (first < length) memcpy(data + first, ring->buffer, length - first);
    ring->tail = length >= space ? length - space : ring->tail + length;
}

bool bsp_frame_ring_push(bsp_frame_ring_t *ring, const uint8_t *data, uint16_t length) {
    const size_t frame_size = sizeof(uint16_t) + length;
    if (frame_size > sizeof(ring->buffer) - ring->used) return false;

    const uint8_t header[sizeof(uint16_t)] = {
        (uint8_t) (length & 0xffU),
        (uint8_t) (length >> 8U),
    };
    ring_write(ring, header, sizeof(header));
    ring_write(ring, data, length);
    ring->used = (uint16_t) (ring->used + frame_size);
    return true;
}

bool bsp_frame_ring_pop(bsp_frame_ring_t *ring, uint8_t *data, uint16_t *length) {
    if (ring->used < sizeof(uint16_t)) return false;

    uint8_t header[sizeof(uint16_t)];
    const uint16_t tail = ring->tail;
    ring_read(ring, header, sizeof(header));
    const uint16_t frame_length = (uint16_t) (header[0] | ((uint16_t) header[1] << 8U));
    if (frame_length > ring->used - sizeof(uint16_t)) {
        ring->tail = tail;
        return false;
    }
    ring_read(ring, data, frame_length);
    ring->used = (uint16_t) (ring->used - sizeof(uint16_t) - frame_length);
    *length = frame_length;
    return true;
}

void bsp_frame_ring_reset(bsp_frame_ring_t *ring) {
    ring->head = 0;
    ring->tail = 0;
    ring->used = 0;
}
