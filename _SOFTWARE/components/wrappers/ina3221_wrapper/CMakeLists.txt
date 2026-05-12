idf_component_register(
    SRCS "ina3221_wrapper.c"
    INCLUDE_DIRS "include"
    REQUIRES driver ina3221 status esp_driver_i2c
)