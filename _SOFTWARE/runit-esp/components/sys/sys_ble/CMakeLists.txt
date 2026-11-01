# NimBLE peripheral: the registry and the stack (core/), the runIT service as two providers
# (link/), the `ble` router (router/).
idf_component_register(
    SRCS "core/sys_ble.c" "core/sys_ble_stack.c" "link/sys_ble_link.c" "router/sys_ble_router.c"
    INCLUDE_DIRS "core" "link" "router" "errors"
    REQUIRES errors bt sys_data_provider
    PRIV_REQUIRES cmd router utils log nvs_flash esp_ringbuf
)
