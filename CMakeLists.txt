cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME trobot)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

option(TROBOT_ENABLE_SYSTEMVIEW "Enable SEGGER SystemView tracing" OFF)
set(TROBOT_USB_CDC_COUNT 1 CACHE STRING "Number of USB CDC ports (0..3)")
set_property(CACHE TROBOT_USB_CDC_COUNT PROPERTY STRINGS 0 1 2 3)
if(NOT TROBOT_USB_CDC_COUNT MATCHES "^[0-3]$")
    message(FATAL_ERROR "TROBOT_USB_CDC_COUNT must be between 0 and 3")
endif()
option(TROBOT_USB_WINUSB "Enable the USB WinUSB transport" ON)

# Enable CMake support for ASM and C languages
enable_language(C CXX ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# INS 启动诊断使用 newlib-nano 的浮点输出。
target_link_options(${CMAKE_PROJECT_NAME} PRIVATE
    -Wl,-u,_printf_float
)

add_subdirectory(bsp)
if(TROBOT_ENABLE_SYSTEMVIEW)
    target_link_libraries(FreeRTOS PRIVATE bsp::segger_config)
    target_compile_options(FreeRTOS PRIVATE
        "$<$<COMPILE_LANGUAGE:C>:-include${CMAKE_CURRENT_SOURCE_DIR}/bsp/internal/segger/freertos_trace_config.h>"
    )
endif()
message(STATUS "BSP Loaded")

add_subdirectory(components)

add_subdirectory(app)
message(STATUS "APP Loaded")

target_compile_options(app PRIVATE -Werror)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    # Add user defined libraries
    stm32cubemx
    app
)


add_custom_target(flash_and_verify
    COMMAND openocd
        -f ${CMAKE_CURRENT_LIST_DIR}/stlink.cfg
        -c "init"
        -c "reset halt"
        -c "program $<TARGET_FILE:${CMAKE_PROJECT_NAME}> verify reset exit"
    DEPENDS ${CMAKE_PROJECT_NAME}
    COMMENT "Flashing and verifying the firmware via OpenOCD"
    USES_TERMINAL
)
