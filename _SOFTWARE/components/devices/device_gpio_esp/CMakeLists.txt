idf_component_register(
    SRCS "adapter_gpio_esp.c" "esp_adc_config.c"
    INCLUDE_DIRS "include"
    REQUIRES status utils sys_io sys_device
    PRIV_REQUIRES esp_driver_gpio esp_driver_ledc esp_adc esp_timer
)
