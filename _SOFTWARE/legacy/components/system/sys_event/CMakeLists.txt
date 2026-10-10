idf_component_register(
    SRCS "sys_event.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES utils
    REQUIRES sys_errors 
)
