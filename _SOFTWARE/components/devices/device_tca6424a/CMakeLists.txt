idf_component_register(
    SRCS "driver_tca6424a.c" "adapter_tca6424a.c"
    INCLUDE_DIRS "include"
    REQUIRES status sys_io sys_i2c sys_device
    PRIV_REQUIRES esp_driver_i2c utils
)
