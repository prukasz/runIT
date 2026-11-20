# ESP_LOG capture, log/error routing options, and recent error record ring buffer (SYS_LOG.MD).
# WHOLE_ARCHIVE: the router registers itself (ROUTER_MODULE_REGISTER) from a source nothing else references.
idf_component_register(
    SRCS "sys_log.c" "router/sys_log_router.c"
    INCLUDE_DIRS "." "errors" "router"
    REQUIRES cmd errors router utils
    PRIV_REQUIRES data_matrix freertos log
    WHOLE_ARCHIVE
)
