idf_component_register(
    SRCS "sys_log.c" "sys_log_router.c"
    INCLUDE_DIRS "." "errors"
    REQUIRES errors utils cmd sys_connector
    PRIV_REQUIRES router log
)
