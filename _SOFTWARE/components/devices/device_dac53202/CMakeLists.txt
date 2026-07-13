idf_component_register(
    SRCS "driver_dac53202.c" "adapter_dac53202.c"
    INCLUDE_DIRS "include"
    REQUIRES status utils sys_i2c sys_device sys_io
)
