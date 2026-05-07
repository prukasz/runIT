idf_component_register(
    SRCS "tca6424a_wrapper.c"
    INCLUDE_DIRS "include"
    REQUIRES driver tca6424a mock_tca status esp_driver_i2c
)