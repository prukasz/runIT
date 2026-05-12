idf_component_register(
    SRCS "ads7128.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_i2c mock_ads
)