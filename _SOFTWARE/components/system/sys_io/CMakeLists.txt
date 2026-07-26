idf_component_register(
    SRCS "sys_io.c"
    INCLUDE_DIRS "include"
    REQUIRES utils sys_errors sys_device callbacks
)