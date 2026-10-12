# WHOLE_ARCHIVE: the error tag is registered by a constructor in sys_log.c.
idf_component_register(
    SRCS "sys_log.c" "sys_log_router.c"
    INCLUDE_DIRS "."
    REQUIRES errors utils cmd sys_connector
    PRIV_REQUIRES router log
    WHOLE_ARCHIVE
)
