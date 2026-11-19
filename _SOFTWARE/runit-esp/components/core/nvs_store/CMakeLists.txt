# The board's non-volatile store (NVS_STORE.MD): its interface (nvs_store.h), what is built on it (nvs_store.c: a JSON
# file of chunks) and this chip's implementation (esp/: ESP-IDF's nvs_flash).
idf_component_register(
    SRCS "nvs_store.c" "esp/nvs_store_esp.c"
    INCLUDE_DIRS "." "errors"
    REQUIRES errors
    PRIV_REQUIRES utils log nvs_flash
)
