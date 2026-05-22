idf_component_register(
    SRCS "manager_i2c.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES utils esp_driver_gpio
    REQUIRES esp_ringbuf status esp_driver_i2c 
)
