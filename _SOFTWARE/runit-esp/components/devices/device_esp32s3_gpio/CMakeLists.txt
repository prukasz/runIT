# WHOLE_ARCHIVE: the type registers itself (SYS_DEVICE_TYPE_REGISTER) and nothing else references it.
idf_component_register(
    SRCS "esp32s3_gpio_pwm.c" "device_esp32s3_gpio.c" "esp32s3_gpio_adc.c"
    INCLUDE_DIRS "."
    REQUIRES errors utils cmd sys_device sys_io sys_event
    PRIV_REQUIRES esp_driver_gpio esp_driver_ledc esp_adc esp_timer
    WHOLE_ARCHIVE
)
