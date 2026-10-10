idf_component_register(
    SRCS "esp/device_tca6424a.c"
    INCLUDE_DIRS "esp"
    REQUIRES sys_errors sys_io sys_i2c sys_device
    PRIV_REQUIRES utils
)
