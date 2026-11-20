cmake_minimum_required(VERSION 3.22)
project(app VERSION 0.0.1 LANGUAGES C CXX ASM)

set(SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/main/main.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/task/example_task.cc"
)

add_library(app OBJECT ${SOURCES})
target_include_directories(app PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_link_libraries(app PUBLIC components bsp m)

set_target_properties(app PROPERTIES
        C_STANDARD 17
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED YES
        POSITION_INDEPENDENT_CODE NO
)
