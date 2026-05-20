idf_component_register(
    SRCS "manager_power.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES provider_voltage_regulator provider_current_monitor provider_gpio_expander
    REQUIRES status manager_io mock_tca mock_tps
)