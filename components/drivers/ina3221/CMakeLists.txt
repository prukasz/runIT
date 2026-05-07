idf_component_register(
    SRCS "ina3221.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_driver_i2c
)