idf_component_register(
    SRCS "tps55289_wrapper.c"
    INCLUDE_DIRS "include"
    REQUIRES driver tps55289 mock_tps status esp_driver_i2c
)