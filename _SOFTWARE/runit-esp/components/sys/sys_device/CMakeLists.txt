# The registry and everything built on it (contracts, devices) see only cmd; the router is the registry's
# remote interface (sys_device_router.c), private to this component.
idf_component_register(
    SRCS "sys_device_errors.c" "sys_device.c" "sys_device_router.c"
    INCLUDE_DIRS "."
    REQUIRES errors utils cmd
    PRIV_REQUIRES router freertos log
)
