idf_component_register(
    SRCS "manager_io.c"
    INCLUDE_DIRS "include"
    REQUIRES status 
    PRIV_REQUIRES utils
)
