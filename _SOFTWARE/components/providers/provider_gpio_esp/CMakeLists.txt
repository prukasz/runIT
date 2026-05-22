idf_component_register(
    SRCS "provider_gpio_esp.c" "esp_adc_config.c"
    INCLUDE_DIRS "include"
    REQUIRES status utils manager_io
    PRIV_REQUIRES esp_driver_gpio esp_driver_ledc esp_adc manager_io esp_timer
)
