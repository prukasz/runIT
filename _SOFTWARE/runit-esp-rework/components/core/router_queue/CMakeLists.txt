# The queue and the two tasks that run the commands (router_queue.h). Nothing below the session needs it: a device or a
# module router compiles with router alone.
idf_component_register(
    SRCS "router_queue.c"
    INCLUDE_DIRS "." "errors"
    REQUIRES errors utils router
    PRIV_REQUIRES esp_ringbuf
    WHOLE_ARCHIVE
)
