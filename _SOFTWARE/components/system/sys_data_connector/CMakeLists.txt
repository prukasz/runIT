idf_component_register(
    SRCS "sys_data_connector.c" "sys_data_connector_ble.c"
    INCLUDE_DIRS "include"
    REQUIRES freertos log sys_errors ble
)

