idf_component_register(
    SRCS "manager_i2c.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_ringbuf status freertos esp_common esp_driver_i2c esp_driver_gpio
)
