include("${CMAKE_CURRENT_LIST_DIR}/../../device.cmake")
runit_device(
    SRCS "device_esp32s3_gpio.c" "esp32s3_gpio_pwm.c" "esp32s3_gpio_adc.c"
    PRIV_REQUIRES errors time utils cmd sys_device sys_event sys_io esp_driver_gpio esp_driver_ledc esp_adc
)
