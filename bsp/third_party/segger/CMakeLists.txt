set(SEGGER_RTT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/rtt")
set(SEGGER_SYSTEMVIEW_DIR "${CMAKE_CURRENT_SOURCE_DIR}/systemview")
set(TROBOT_SEGGER_CONFIG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../internal/segger")

add_library(bsp_segger_config INTERFACE)
add_library(bsp::segger_config ALIAS bsp_segger_config)
target_compile_definitions(bsp_segger_config INTERFACE
    TROBOT_ENABLE_SYSTEMVIEW=1
    SYSVIEW_FREERTOS_MAX_NOF_TASKS=16
)
target_include_directories(bsp_segger_config INTERFACE
    "${TROBOT_SEGGER_CONFIG_DIR}"
    "${SEGGER_RTT_DIR}/RTT"
    "${SEGGER_SYSTEMVIEW_DIR}/Config"
    "${SEGGER_SYSTEMVIEW_DIR}/SEGGER"
    "${SEGGER_SYSTEMVIEW_DIR}/SYSVIEW"
    "${SEGGER_SYSTEMVIEW_DIR}/FreeRTOS"
)

add_library(bsp_segger STATIC
    "${SEGGER_RTT_DIR}/RTT/SEGGER_RTT.c"
    "${SEGGER_SYSTEMVIEW_DIR}/SYSVIEW/SEGGER_SYSVIEW.c"
    "${SEGGER_SYSTEMVIEW_DIR}/FreeRTOS/SEGGER_SYSVIEW_FreeRTOS.c"
)
set_source_files_properties(
    "${SEGGER_SYSTEMVIEW_DIR}/FreeRTOS/SEGGER_SYSVIEW_FreeRTOS.c"
    PROPERTIES COMPILE_OPTIONS
        "-include${TROBOT_SEGGER_CONFIG_DIR}/freertos_trace_config.h"
)
add_library(bsp::segger ALIAS bsp_segger)
target_link_libraries(bsp_segger
    PRIVATE bsp_segger_config
    PUBLIC stm32cubemx
)
set_target_properties(bsp_segger PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED YES
    POSITION_INDEPENDENT_CODE NO
)
