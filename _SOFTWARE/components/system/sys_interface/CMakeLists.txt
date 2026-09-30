idf_component_register(SRCS "sys_interface.c"
                    INCLUDE_DIRS "include"
                    REQUIRES freertos log sys_errors sys_data_connector sys_buffers utils)
