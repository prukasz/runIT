idf_component_register(
    SRCS "sys_io.c"
    INCLUDE_DIRS "include"
    REQUIRES status 
    PRIV_REQUIRES utils sys_device
)
