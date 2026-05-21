idf_component_register(
    SRCS
    INCLUDE_DIRS "include"
    REQUIRES status 
    PRIV_REQUIRES esp_driver_gpio esp_driver_ledc esp_adc manager_io
)
