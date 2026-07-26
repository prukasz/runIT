file(GLOB code_items RELATIVE ${CMAKE_CURRENT_LIST_DIR} "codes/*")
set(code_dirs "")
foreach(item ${code_items})
    if(IS_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/${item}")
        list(APPEND code_dirs "${item}")
    endif()
endforeach()

idf_component_register(
    SRCS "sys_error.c"
    INCLUDE_DIRS "include" ${code_dirs} "codes"
                 "../system/sys_device/include"
                 "../system/sys_io/include"
                 "../system/sys_power/include"
                 "../system/sys_i2c/include"
                 "../system/ble/include"
                 "../system/sys_interface/include"
    REQUIRES esp_ringbuf
    PRIV_REQUIRES utils ble
)