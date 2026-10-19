idf_component_register(
    SRCS "router.c" "router_dispatch.c" "router_err.c" "router_sys.c"
    INCLUDE_DIRS "."
    REQUIRES errors utils cmd
    PRIV_REQUIRES log esp_system esp_ringbuf
)
