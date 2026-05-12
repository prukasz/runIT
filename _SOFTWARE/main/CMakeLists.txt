idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES runit_kernel driver status
)