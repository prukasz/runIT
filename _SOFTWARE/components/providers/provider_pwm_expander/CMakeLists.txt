idf_component_register(
    SRCS "provider_pwm_expander.c"
    INCLUDE_DIRS "include"
    REQUIRES status manager_io
    PRIV_REQUIRES pca9685 esp_driver_i2c
)
