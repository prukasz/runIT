# WHOLE_ARCHIVE: the error tags are registered by a constructor in sys_power_manager.c.
idf_component_register(
    SRCS "sys_power_manager.c" "sys_power_manager_router.c"
    INCLUDE_DIRS "." "errors"
    REQUIRES errors utils cmd sys_device sys_event sys_io sys_power
    PRIV_REQUIRES router freertos
    WHOLE_ARCHIVE
)
