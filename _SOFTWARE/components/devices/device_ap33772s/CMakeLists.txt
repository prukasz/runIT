idf_component_register(
    SRCS "esp/device_ap33772s.c"
    INCLUDE_DIRS "esp"
    REQUIRES sys_errors sys_io sys_i2c sys_device sys_power
    PRIV_REQUIRES esp_driver_i2c
)
