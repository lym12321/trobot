cmake_minimum_required(VERSION 3.22)
project(bsp VERSION 0.0.1 LANGUAGES C CXX ASM)

file(GLOB_RECURSE SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/flash/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/segger/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/segger/*.S"
    "${CMAKE_CURRENT_SOURCE_DIR}/usb_device/*.c"
)

add_library(bsp STATIC ${SOURCES})
target_include_directories(bsp
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/segger>
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/segger/Config>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/internal
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/flash/w25q64
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/flash/easyflash
)

target_link_libraries(bsp PUBLIC stm32cubemx m)

set_target_properties(bsp PROPERTIES
        C_STANDARD 17
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED YES
        POSITION_INDEPENDENT_CODE NO
)