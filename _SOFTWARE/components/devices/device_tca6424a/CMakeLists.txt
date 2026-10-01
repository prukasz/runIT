idf_component_register(
    SRCS "device_tca6424a.c"
    INCLUDE_DIRS "include"
    REQUIRES sys_errors sys_io sys_i2c sys_device
    PRIV_REQUIRES utils
)
