idf_component_register(
    SRCS "esp/device_drv8962.c"
    INCLUDE_DIRS "esp"
    REQUIRES sys_hbridge sys_device sys_io sys_errors utils sys_event
)
