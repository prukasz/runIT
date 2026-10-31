idf_component_register(
    SRCS "router.c" "router_dispatch.c" "router_link.c"
    INCLUDE_DIRS "." "errors"
    REQUIRES errors utils cmd
    PRIV_REQUIRES log esp_ringbuf
)
