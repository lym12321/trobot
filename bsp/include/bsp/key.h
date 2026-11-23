#pragma once

#include "bsp/def.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    E_KEY_MIDDLE, E_KEY_UP, E_KEY_DOWN, E_KEY_LEFT, E_KEY_RIGHT, E_KEY_NONE
} bsp_key_e;

typedef enum {
    E_KEY_CLICK, E_KEY_DOUBLE_CLICK, E_KEY_LONG_PRESS
} bsp_key_event_e;

typedef void (*bsp_key_cb_t)(bsp_key_e key, bsp_key_event_e event, void *ctx);

// Register before polling; cb and ctx must remain valid through delivery.
bsp_status_t bsp_key_set_callback(bsp_key_e key, bsp_key_event_e event, bsp_key_cb_t cb, void *ctx);
// One task calls this at most 10 ms apart; callbacks run synchronously in that task.
void bsp_key_poll(void);

#ifdef __cplusplus
}
#endif
