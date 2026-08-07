#ifndef BSP_INTERNAL_SEGGER_TRACE_H
#define BSP_INTERNAL_SEGGER_TRACE_H

#if defined(TROBOT_ENABLE_SYSTEMVIEW)

void bsp_segger_trace_init(void);

#else

static inline void bsp_segger_trace_init(void) {
}

#endif

#endif
