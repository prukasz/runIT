file(GLOB device_paths "${CMAKE_CURRENT_LIST_DIR}/../*")
set(device_components "")
foreach(entry ${device_paths})
    if(IS_DIRECTORY "${entry}" AND EXISTS "${entry}/CMakeLists.txt")
        get_filename_component(comp_name "${entry}" NAME)
        if(NOT comp_name STREQUAL "devices")
            list(APPEND device_components "${comp_name}")
        endif()
    endif()
endforeach()

idf_component_register(
    SRCS "devices.c"
    INCLUDE_DIRS "include"
    REQUIRES ${device_components} sys_device sys_errors
)
