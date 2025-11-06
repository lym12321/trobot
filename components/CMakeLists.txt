cmake_minimum_required(VERSION 3.22)

file(GLOB _COMP_DIRS CONFIGURE_DEPENDS LIST_DIRECTORIES TRUE "${CMAKE_CURRENT_SOURCE_DIR}/*")

foreach(p ${_COMP_DIRS})
    if(IS_DIRECTORY "${p}" AND EXISTS "${p}/CMakeLists.txt")
        get_filename_component(_name "${p}" NAME)
        add_subdirectory("${p}" "${_name}")
    endif()
endforeach()

add_library(components INTERFACE)

foreach(p ${_COMP_DIRS})
    if(IS_DIRECTORY "${p}" AND EXISTS "${p}/CMakeLists.txt")
        # 从该目录作用域读取本目录定义的目标
        get_property(_tgts DIRECTORY "${p}" PROPERTY BUILDSYSTEM_TARGETS)
        foreach(t ${_tgts})
            if(TARGET "components::${t}")
                target_link_libraries(components INTERFACE "components::${t}")
                message(STATUS "Component '${t}' Loaded")
            endif()
        endforeach()
    endif()
endforeach()