idf_component_register(
    SRCS "ads7128_mock.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_driver_i2c
)
