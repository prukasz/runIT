# The router is this module's remote interface and its urgent task runs the safe state; the session's gone peers are the
# dead-man.
idf_component_register(
    SRCS "sys_system.c" "router/sys_system_router.c"
    INCLUDE_DIRS "." "router"
    REQUIRES errors utils cmd router time sys_session
    PRIV_REQUIRES log esp_system
)
