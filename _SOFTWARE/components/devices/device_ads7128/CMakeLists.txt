idf_component_register(
    SRCS "driver_ads7128.c" "adapter_ads7128.c"
    INCLUDE_DIRS "include"
    REQUIRES status sys_io sys_i2c sys_device
)
