idf_component_register(
    SRCS "sys_callbacks.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES utils 
    REQUIRES status 
)
