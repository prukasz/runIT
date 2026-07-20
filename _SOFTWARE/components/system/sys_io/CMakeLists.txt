idf_component_register(
    SRCS "sys_io.c"
    INCLUDE_DIRS "include"
    REQUIRES status callbacks
    PRIV_REQUIRES utils sys_device
)
