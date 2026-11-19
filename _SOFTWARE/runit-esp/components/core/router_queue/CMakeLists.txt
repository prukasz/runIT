# The queue and the two tasks that run the commands, the router's port and this board's calls (router_queue.h). A device
# or a module router compiles with router alone.
idf_component_register(
    SRCS "router_queue.c" "router_request.c"
    INCLUDE_DIRS "." "errors"
    PRIV_INCLUDE_DIRS "internal"
    REQUIRES errors utils router data_matrix
    PRIV_REQUIRES esp_ringbuf timer
    WHOLE_ARCHIVE
)
