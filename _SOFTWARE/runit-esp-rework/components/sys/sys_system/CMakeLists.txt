# The board's system commands, the safe state (every device suspended) and the dead-man (the sessions' silence).
# WHOLE_ARCHIVE: the router registers itself (ROUTER_MODULE_REGISTER) from a source nothing else references.
idf_component_register(
    SRCS "sys_system.c" "router/sys_system_router.c"
    INCLUDE_DIRS "." "router"
    REQUIRES errors utils cmd router
    PRIV_REQUIRES router_queue timer time sys_session sys_device log esp_system
    WHOLE_ARCHIVE
)
