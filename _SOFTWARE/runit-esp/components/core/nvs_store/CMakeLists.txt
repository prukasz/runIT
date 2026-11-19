# The board's non-volatile store (NVS_STORE.MD): its API with no implementation in it (nvs_store.h / .c: the calls
# through the interface, a JSON file of chunks), the interface an implementation fills (nvs_store_iface.h) and this
# chip's implementation (esp/: ESP-IDF's nvs_flash).
idf_component_register(
    SRCS "nvs_store.c" "esp/nvs_store_esp.c"
    INCLUDE_DIRS "." "errors"
    REQUIRES errors
    PRIV_REQUIRES utils log nvs_flash
)
