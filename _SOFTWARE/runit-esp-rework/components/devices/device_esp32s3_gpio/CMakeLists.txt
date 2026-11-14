# The ESP32-S3's own pins as one device: the driver and its two parts (PWM on LEDC, the ADC task), with a header private
# to this folder; nothing includes them (no INCLUDE_DIRS). WHOLE_ARCHIVE: the type registers itself
# (SYS_DEVICE_TYPE_DEFINE) and nothing references it.
idf_component_register(
    SRCS "device_esp32s3_gpio.c" "esp32s3_gpio_pwm.c" "esp32s3_gpio_adc.c"
    PRIV_REQUIRES errors time utils cmd sys_device sys_event sys_io esp_driver_gpio esp_driver_ledc esp_adc
    WHOLE_ARCHIVE
)
