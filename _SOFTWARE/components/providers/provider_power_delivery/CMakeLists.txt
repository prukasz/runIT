idf_component_register(
    SRCS "provider_power_delivery.c"
    INCLUDE_DIRS "include"
    REQUIRES status manager_io 
    PRIV_REQUIRES esp_driver_i2c ap33772s
)
