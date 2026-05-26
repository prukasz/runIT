idf_component_register(
    SRCS "provider_adc_expander.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES ads7128 mock_ads manager_io
    REQUIRES status
)
