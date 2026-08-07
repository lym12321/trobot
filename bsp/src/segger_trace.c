#include "segger/trace.h"

#if defined(TROBOT_ENABLE_SYSTEMVIEW)

#include "FreeRTOS.h"
#include "SEGGER_SYSVIEW.h"

extern const SEGGER_SYSVIEW_OS_API SYSVIEW_X_OS_TraceAPI;

static void send_system_description(void) {
    SEGGER_SYSVIEW_SendSysDesc(
        "N=" SEGGER_SYSVIEW_APP_NAME
        ",D=" SEGGER_SYSVIEW_DEVICE_NAME
        ",O=FreeRTOS"
    );
    SEGGER_SYSVIEW_SendSysDesc("I#15=SysTick");
}

void SEGGER_SYSVIEW_Conf(void) {
    SEGGER_SYSVIEW_Init(
        configCPU_CLOCK_HZ,
        configCPU_CLOCK_HZ,
        &SYSVIEW_X_OS_TraceAPI,
        send_system_description
    );
    SEGGER_SYSVIEW_SetRAMBase(0x20000000u);
}

void bsp_segger_trace_init(void) {
    SEGGER_SYSVIEW_Conf();
}

#endif
