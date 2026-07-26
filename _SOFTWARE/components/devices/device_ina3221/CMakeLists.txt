idf_component_register(
    SRCS "driver_ina3221.c" "adapter_ina3221.c"
    INCLUDE_DIRS "include"
    REQUIRES sys_errors utils sys_i2c sys_device sys_io sys_power
)
