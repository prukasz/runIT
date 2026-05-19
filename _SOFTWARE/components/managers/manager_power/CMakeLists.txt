idf_component_register(
    SRCS "manager_power.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES tps55289_wrapper ina3221_wrapper
    REQUIRES status manager_io mock_tca mock_tps
)