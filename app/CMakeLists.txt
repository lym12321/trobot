cmake_minimum_required(VERSION 3.22)
project(app VERSION 0.0.1 LANGUAGES C CXX ASM)

function(recursion_include path)
    get_filename_component(_abs_path "${path}" REALPATH)
    get_filename_component(_cur_dir  "${CMAKE_CURRENT_SOURCE_DIR}" REALPATH)

    if (IS_DIRECTORY "${path}")
        if (NOT _abs_path STREQUAL _cur_dir)
            file(RELATIVE_PATH _rel "${CMAKE_CURRENT_SOURCE_DIR}" "${_abs_path}")
            string(REPLACE "\\" "/" _rel "${_rel}")
            message(STATUS "Application '${_rel}' Loaded")
            include_directories("${path}")
        endif()
    endif()

    file(GLOB ALL_SUB RELATIVE "${path}" "${path}/*")
    foreach(sub ${ALL_SUB})
        if (IS_DIRECTORY "${path}/${sub}")
            recursion_include("${path}/${sub}")
        endif()
    endforeach()
endfunction()

file(GLOB_RECURSE SOURCES ${sources} "${CMAKE_CURRENT_SOURCE_DIR}/*.*")
recursion_include(${CMAKE_CURRENT_SOURCE_DIR})

add_library(app OBJECT ${SOURCES})
target_include_directories(app PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_link_libraries(app PUBLIC components bsp m)

set_target_properties(app PROPERTIES
        C_STANDARD 17
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED YES
        POSITION_INDEPENDENT_CODE NO
)