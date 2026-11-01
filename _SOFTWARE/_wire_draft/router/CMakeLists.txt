# WHOLE_ARCHIVE: the error tags are registered by a constructor nothing else references: the linker would drop it.
idf_component_register(
    SRCS "router.c" "router_dispatch.c" "errors/router_errors.c"
    INCLUDE_DIRS "." "errors"
    REQUIRES cmd errors utils
    PRIV_REQUIRES esp_ringbuf
    WHOLE_ARCHIVE
)
