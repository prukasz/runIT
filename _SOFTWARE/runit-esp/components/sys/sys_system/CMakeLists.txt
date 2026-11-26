# The board's system commands, the safe state (the event SAFE, every device suspended) and the dead-man (the sessions'
# silence). sys_event is public: sys_system_events.h declares SAFE.
# WHOLE_ARCHIVE: the router registers itself (ROUTER_MODULE_REGISTER) from a source nothing else references.
idf_component_register(
    SRCS "sys_system.c" "router/sys_system_router.c"
    INCLUDE_DIRS "." "router"
    REQUIRES errors utils cmd router sys_event
    PRIV_REQUIRES router_queue timer time sys_session sys_device log esp_system
    WHOLE_ARCHIVE
)
