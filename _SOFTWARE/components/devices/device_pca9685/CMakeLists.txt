idf_component_register(
    SRCS "driver_pca9685.c" "adapter_pca9685.c"
    INCLUDE_DIRS "include"
    REQUIRES sys_errors sys_io sys_i2c sys_device
    PRIV_REQUIRES esp_driver_i2c
)
