#include "bsp/key.h"

#include "bsp/adc.h"
#include "bsp/time.h"

typedef struct {
    bsp_key_cb_t cb;
    void *ctx;
} key_callback_t;

static key_callback_t callbacks[E_KEY_NONE][E_KEY_LONG_PRESS + 1];
static struct {
    bsp_key_e candidate, stable, pending;
    uint32_t candidate_since, pressed_at, released_at;
    unsigned int long_press : 1;
    unsigned int second_press : 1;
} keys = {.candidate = E_KEY_NONE, .stable = E_KEY_NONE, .pending = E_KEY_NONE};

bsp_status_t bsp_key_set_callback(bsp_key_e key, bsp_key_event_e event, bsp_key_cb_t cb, void *ctx) {
    if ((unsigned) key >= E_KEY_NONE || (unsigned) event > E_KEY_LONG_PRESS) return BSP_STATUS_ERROR;
    callbacks[key][event] = (key_callback_t){cb, ctx};
    return BSP_STATUS_OK;
}

static void dispatch(bsp_key_e key, bsp_key_event_e event) {
    const key_callback_t entry = callbacks[key][event];
    if (entry.cb) entry.cb(key, event, entry.ctx);
}

static bsp_key_e decode(uint16_t raw) {
    if (raw < 6554U) return E_KEY_MIDDLE;
    if (raw < 19661U) return E_KEY_LEFT;
    if (raw < 32768U) return E_KEY_RIGHT;
    if (raw < 45875U) return E_KEY_UP;
    if (raw < 58982U) return E_KEY_DOWN;
    return E_KEY_NONE;
}

void bsp_key_poll(void) {
    const uint32_t now = bsp_time_get_ms();
    const bsp_key_e sample = decode(bsp_adc_lcd_key_raw());
    if (sample != keys.candidate) {
        keys.candidate = sample;
        keys.candidate_since = now;
    }
    if (sample != keys.stable && now - keys.candidate_since >= 10) {
        const bsp_key_e released = keys.stable;
        keys.stable = sample;
        if (released != E_KEY_NONE && !keys.long_press) {
            if (keys.second_press) {
                keys.pending = E_KEY_NONE;
                dispatch(released, E_KEY_DOUBLE_CLICK);
            } else if (callbacks[released][E_KEY_DOUBLE_CLICK].cb) {
                keys.pending = released;
                keys.released_at = now;
            } else {
                dispatch(released, E_KEY_CLICK);
            }
        }
        keys.long_press = 0;
        keys.second_press = 0;
        if (sample != E_KEY_NONE) {
            keys.pressed_at = now;
            if (keys.pending != E_KEY_NONE) {
                if (sample == keys.pending && now - keys.released_at < 500) {
                    keys.second_press = 1;
                } else {
                    dispatch(keys.pending, E_KEY_CLICK);
                    keys.pending = E_KEY_NONE;
                }
            }
        }
    }
    if (keys.stable != E_KEY_NONE && !keys.long_press && now - keys.pressed_at >= 800) {
        keys.long_press = 1;
        keys.second_press = 0;
        keys.pending = E_KEY_NONE;
        dispatch(keys.stable, E_KEY_LONG_PRESS);
    }
    if (keys.pending != E_KEY_NONE && !keys.second_press && now - keys.released_at >= 500) {
        dispatch(keys.pending, E_KEY_CLICK);
        keys.pending = E_KEY_NONE;
    }
}
