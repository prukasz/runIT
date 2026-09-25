file(GLOB code_items RELATIVE ${CMAKE_CURRENT_LIST_DIR} "codes/*")
set(code_dirs "")
foreach(item ${code_items})
    if(IS_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/${item}")
        list(APPEND code_dirs "${item}")
    endif()
endforeach()

idf_component_register(
    SRCS "sys_error.c" "sys_error_handler.c" "sys_error_log.c"
    INCLUDE_DIRS "include" ${code_dirs} "codes"
                 # Error maps of every module (tags + owners), aggregated by
                 # codes/sys_error_codes.h. Each errors/ folder holds only error
                 # maps, so no module API leaks to sys_error.h users. The modules
                 # REQUIRE sys_errors, so these are plain dirs, not REQUIRES (a
                 # cycle): IDF warns once per dir, which is expected (SYS_ERRORS.MD).
                 "../system/sys_device/errors"
                 "../system/sys_io/errors"
                 "../system/sys_power/errors"
                 "../system/sys_i2c/errors"
                 "../system/sys_interface/errors"
                 "../system/sys_buffers/errors"
                 "../system/sys_actions/errors"
                 "../system/ble/errors"
                 "../system/sys_event/errors"
                 "../system/sys_hbridge/errors"
                 "../system/sys_data_connector/errors"
                 "../system/sys_settings/errors"
                 "../system/sys_uart/errors"
                 "../VM/errors"
                 "../devices/devices/errors"
                 "../features/errors"
                 "../runit/errors"
    REQUIRES esp_ringbuf
    PRIV_REQUIRES utils
)