idf_component_register(
    SRCS "manager_io.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES tca6424a mock_tca
    REQUIRES status tca6424a_wrapper
)