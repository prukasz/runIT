idf_component_register(
    SRCS "sys_buffers.c"
    INCLUDE_DIRS "include"
    REQUIRES sys_errors esp_ringbuf utils
)
