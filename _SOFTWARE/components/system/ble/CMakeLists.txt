idf_component_register(
    SRCS "sys_ble.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_ringbuf a_ble_driver sys_device status utils callbacks
)
