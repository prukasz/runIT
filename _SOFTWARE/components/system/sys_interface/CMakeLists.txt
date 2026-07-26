idf_component_register(SRCS "sys_interface.c"
                    INCLUDE_DIRS "include"
                    REQUIRES freertos log ble codecs sys_errors sys_device sys_io sys_power)
