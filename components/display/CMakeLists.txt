cmake_minimum_required(VERSION 3.22)
project(display VERSION 0.0.1 LANGUAGES CXX)

add_library(display STATIC src/st7789.cc src/graphics.cc)
target_compile_features(display PUBLIC cxx_std_23)
target_include_directories(display
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

set_target_properties(display PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED YES
    POSITION_INDEPENDENT_CODE NO
)

add_library(components::display ALIAS display)
