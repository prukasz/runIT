idf_component_register(
    SRCS "sys_device.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES utils sys_io
    REQUIRES status 
)
