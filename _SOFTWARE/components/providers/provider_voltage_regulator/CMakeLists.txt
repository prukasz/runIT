idf_component_register(
    SRCS "provider_voltage_regulator.c"
    INCLUDE_DIRS "include"
    REQUIRES status
    PRIV_REQUIRES tps55289 mock_tps
)
