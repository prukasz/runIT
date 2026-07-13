idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES status sys_device sys_i2c sys_io sys_power ble runit
)