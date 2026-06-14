idf_component_register(
    SRCS "provider_dac_expander.c"
    INCLUDE_DIRS "include"
    REQUIRES dac53202 status utils
)
