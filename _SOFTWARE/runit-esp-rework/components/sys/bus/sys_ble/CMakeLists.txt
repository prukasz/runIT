# The board's BLE peripheral (SYS_BLE.MD): NimBLE, the run-time GATT table, the framing, a hook per characteristic and
# its router. No provider is made here (provider_ble). WHOLE_ARCHIVE: the router registers itself (ROUTER_MODULE_REGISTER)
# from a source nothing else references.
idf_component_register(
    SRCS "sys_ble.c" "sys_ble_stack.c" "router/sys_ble_router.c"
    INCLUDE_DIRS "." "errors" "router"
    PRIV_INCLUDE_DIRS "internal"
    REQUIRES errors cmd router
    PRIV_REQUIRES bt utils log esp_ringbuf
    WHOLE_ARCHIVE
)
