idf_component_register(
    SRCS "sys_remote.c"
    INCLUDE_DIRS "."
    REQUIRES errors
    PRIV_REQUIRES cmd router sys_device sys_session sys_watch sys_data_connector sys_event
)
