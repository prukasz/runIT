idf_component_register(
    SRCS "tps55289_mock.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_driver_i2c
)
