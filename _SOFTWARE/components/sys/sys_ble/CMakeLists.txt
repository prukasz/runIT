idf_component_register(
    SRCS "sys_ble.c" "sys_ble_stack.c" "sys_ble_link.c" "sys_ble_router.c"
    INCLUDE_DIRS "."
    REQUIRES errors bt
    PRIV_REQUIRES cmd router utils log nvs_flash esp_ringbuf sys_connector
)
