# The router is this module's remote interface and the source of the link watch (frames, a lost link, the urgent task).
idf_component_register(
    SRCS "sys_system.c" "router/sys_system_router.c"
    INCLUDE_DIRS "." "router"
    REQUIRES errors utils cmd router time
    PRIV_REQUIRES log esp_system
)
