idf_component_register(
    SRCS "manager_ble.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_ringbuf a_ble_driver
)
