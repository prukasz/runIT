idf_component_register(
    SRCS "tca6424a_mock.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_driver_i2c
)
