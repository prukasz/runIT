idf_component_register(
    SRCS "esp/esp_pwm.c" "esp/device_gpio_esp.c" "esp/esp_adc_config.c"
    INCLUDE_DIRS "esp"
    REQUIRES sys_errors utils sys_io sys_device
    PRIV_REQUIRES esp_driver_gpio esp_driver_ledc esp_adc esp_timer
)
