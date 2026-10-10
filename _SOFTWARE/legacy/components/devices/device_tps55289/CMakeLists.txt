idf_component_register(
    SRCS "esp/device_tps55289.c"
    INCLUDE_DIRS "esp"
    REQUIRES sys_errors sys_i2c sys_device sys_io sys_power
)
