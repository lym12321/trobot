cmake_minimum_required(VERSION 3.22)
project(bsp VERSION 0.0.1 LANGUAGES C CXX ASM)

if(TARGET USB_Device_Library)
    message(FATAL_ERROR
        "CherryUSB requires the STM32Cube USB_DEVICE middleware to be disabled. "
        "Disable USB_DEVICE and OTG_HS IRQ handler generation in CubeMX, then regenerate the project."
    )
endif()

set(CHERRYUSB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cherryusb")

if(TROBOT_ENABLE_SYSTEMVIEW)
    add_subdirectory(third_party/segger)
endif()

file(GLOB_RECURSE SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/easyflash/*.c"
)

list(APPEND SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/internal/w25q64/w25q64.c"
    "${CHERRYUSB_DIR}/core/usbd_core.c"
    "${CHERRYUSB_DIR}/class/cdc/usbd_cdc_acm.c"
    "${CHERRYUSB_DIR}/port/dwc2/usb_dc_dwc2.c"
    "${CHERRYUSB_DIR}/port/dwc2/usb_glue_st.c"
)

add_library(bsp STATIC ${SOURCES})
target_include_directories(bsp
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/internal
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/internal/cherryusb
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/internal/w25q64
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/easyflash
    PRIVATE ${CHERRYUSB_DIR}
    PRIVATE ${CHERRYUSB_DIR}/common
    PRIVATE ${CHERRYUSB_DIR}/core
    PRIVATE ${CHERRYUSB_DIR}/class/cdc
    PRIVATE ${CHERRYUSB_DIR}/class/hub
    PRIVATE ${CHERRYUSB_DIR}/port/dwc2
)

target_link_libraries(bsp PUBLIC stm32cubemx m)

if(TROBOT_ENABLE_SYSTEMVIEW)
    target_link_libraries(bsp PRIVATE bsp::segger bsp::segger_config)
endif()

set_target_properties(bsp PROPERTIES
        C_STANDARD 17
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED YES
        POSITION_INDEPENDENT_CODE NO
)
