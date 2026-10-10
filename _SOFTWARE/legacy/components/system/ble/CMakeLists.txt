idf_component_register(
    SRCS "sys_ble.c"
         "sys_ble_stack.c"
         "sys_ble_provider.c"
    INCLUDE_DIRS "include"
                 "."
    REQUIRES bt nvs_flash esp_ringbuf sys_errors utils sys_event sys_buffers sys_data_connector
)
