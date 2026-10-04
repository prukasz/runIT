idf_component_register(
    SRCS "esp/device_servo.c"
    INCLUDE_DIRS "esp"
    REQUIRES sys_errors sys_io sys_device
)
