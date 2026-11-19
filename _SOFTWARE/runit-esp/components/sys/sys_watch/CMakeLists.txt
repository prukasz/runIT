# Watches (SYS_WATCH.MD): server reads pushed on channel 0x80 and cache-only client (P5 + B3).
# WHOLE_ARCHIVE: the router registers itself (ROUTER_MODULE_REGISTER) from a source nothing else references.
idf_component_register(
    SRCS "sys_watch.c" "sys_watch_client.c" "router/sys_watch_router.c"
    INCLUDE_DIRS "." "errors" "router"
    PRIV_INCLUDE_DIRS "internal"
    REQUIRES cmd errors router sys_session data_matrix utils
    PRIV_REQUIRES board_id sys_event router_queue timer time
    WHOLE_ARCHIVE
)
