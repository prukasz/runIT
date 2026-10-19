idf_component_register(
    SRCS "router.c" "router_dispatch.c"
    INCLUDE_DIRS "."
    REQUIRES errors utils cmd
    PRIV_REQUIRES log esp_ringbuf
)
