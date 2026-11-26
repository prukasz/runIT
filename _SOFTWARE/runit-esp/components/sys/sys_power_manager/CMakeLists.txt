# WHOLE_ARCHIVE: the error tags are registered by a constructor in sys_power_manager.c.
idf_component_register(
    SRCS "sys_power_manager.c" "router/sys_power_manager_router.c"
    INCLUDE_DIRS "." "errors" "router"
    REQUIRES errors utils cmd sys_device sys_event sys_io_contract sys_power_contract
    PRIV_REQUIRES router sys_system freertos
    WHOLE_ARCHIVE
)
