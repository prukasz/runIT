idf_component_register(
    SRCS "driver_drv8962.c" "adapter_drv8962.c"
    INCLUDE_DIRS "include" "."
    REQUIRES sys_hbridge sys_device sys_io sys_errors utils sys_event
)

