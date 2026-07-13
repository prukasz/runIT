idf_component_register(
    SRCS "sys_power.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES utils sys_device
    REQUIRES status 
)
