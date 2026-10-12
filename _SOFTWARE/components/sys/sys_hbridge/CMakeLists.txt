idf_component_register(
    SRCS "sys_hbridge.c"
    INCLUDE_DIRS "."
    REQUIRES errors cmd sys_device sys_event
)
