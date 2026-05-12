idf_component_register(
    SRCS "a_ble.c"
         "gap.c"
         "gatt_svc.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES bt nvs_flash esp_ringbuf
)