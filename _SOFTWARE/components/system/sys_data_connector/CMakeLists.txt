idf_component_register(
    SRCS "sys_data_connector.c"
    INCLUDE_DIRS "include"
    REQUIRES freertos log sys_errors utils
)
