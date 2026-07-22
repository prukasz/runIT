idf_component_register(
    SRCS "sys_buffers.c"
    INCLUDE_DIRS "include"
    REQUIRES status esp_ringbuf utils
)
