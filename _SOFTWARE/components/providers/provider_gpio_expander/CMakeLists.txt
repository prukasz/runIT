idf_component_register(
    SRCS "provider_gpio_expander.c"
    INCLUDE_DIRS "include"
    REQUIRES status manager_io
    PRIV_REQUIRES tca6424a mock_tca esp_driver_i2c utils
)
