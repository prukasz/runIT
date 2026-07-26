idf_component_register(
    SRCS "sys_ble.c"
         "sys_ble_stack.c"
    INCLUDE_DIRS "include"
                 "."
    REQUIRES bt nvs_flash esp_ringbuf sys_device sys_errors utils callbacks sys_buffers
)
