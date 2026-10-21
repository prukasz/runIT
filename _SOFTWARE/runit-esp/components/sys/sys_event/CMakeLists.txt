idf_component_register(
    SRCS "sys_event.c"
    INCLUDE_DIRS "." "errors"
    REQUIRES errors utils cmd sys_device time
    PRIV_REQUIRES sys_connector router
)
