idf_component_register(
    SRCS "device_dac53202.c"
    INCLUDE_DIRS "include"
    REQUIRES sys_errors utils sys_i2c sys_device sys_io
)
