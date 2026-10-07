idf_component_register(
    SRCS "esp/device_power_mux.c"
    INCLUDE_DIRS "esp"
    REQUIRES sys_errors sys_io sys_device sys_power
)
