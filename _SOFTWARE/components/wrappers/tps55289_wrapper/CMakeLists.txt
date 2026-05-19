idf_component_register(
    SRCS "tps55289_wrapper.c"
    INCLUDE_DIRS "include"
    REQUIRES status
    PRIV_REQUIRES tps55289 mock_tps
)