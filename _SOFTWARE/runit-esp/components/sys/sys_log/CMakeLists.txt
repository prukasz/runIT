idf_component_register(
    SRCS "sys_log.c" "router/sys_log_router.c"
    INCLUDE_DIRS "." "errors" "router"
    REQUIRES errors utils cmd sys_data_connector
    PRIV_REQUIRES router log
)
