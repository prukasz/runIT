idf_component_register(SRCS "sys_error_handler.c"
                    INCLUDE_DIRS "include"
                    REQUIRES freertos log status
                    PRIV_REQUIRES ble utils sys_power sys_io sys_device)
