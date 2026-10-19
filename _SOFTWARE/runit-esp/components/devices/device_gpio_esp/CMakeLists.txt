# WHOLE_ARCHIVE: the type registers itself (SYS_DEVICE_TYPE_DEFINE) and nothing else references it.
idf_component_register(
    SRCS "esp_pwm.c" "device_gpio_esp.c" "esp_adc_config.c"
    INCLUDE_DIRS "."
    REQUIRES errors utils cmd sys_device sys_io sys_event
    PRIV_REQUIRES esp_driver_gpio esp_driver_ledc esp_adc esp_timer
    WHOLE_ARCHIVE
)
