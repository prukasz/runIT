idf_component_register(
    SRCS "driver_ap33772s.c" "adapter_ap33772s.c"
    INCLUDE_DIRS "include"
    REQUIRES sys_errors sys_io sys_i2c sys_device sys_power
    PRIV_REQUIRES esp_driver_i2c
)
