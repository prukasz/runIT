idf_component_register(
    SRCS "sys_io.c"
    INCLUDE_DIRS "include"
    REQUIRES utils status sys_device callbacks
)