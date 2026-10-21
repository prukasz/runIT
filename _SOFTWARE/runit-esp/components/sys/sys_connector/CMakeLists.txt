idf_component_register(
    SRCS "sys_connector.c" "router/sys_connector_router.c"
    INCLUDE_DIRS "." "errors" "router"
    REQUIRES errors utils
    PRIV_REQUIRES cmd router log esp_ringbuf
)
