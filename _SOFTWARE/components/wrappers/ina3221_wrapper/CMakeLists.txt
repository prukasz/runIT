idf_component_register(
    SRCS "ina3221_wrapper.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES ina3221 
    REQUIRES status
)