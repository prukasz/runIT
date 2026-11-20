# Watches (SYS_WATCH.MD): reads run here, their entries out of the port WATCH; the cached reads of other boards.
# WHOLE_ARCHIVE: the router registers itself (ROUTER_MODULE_REGISTER) from a source nothing else references.
idf_component_register(
    SRCS "sys_watch.c" "sys_watch_client.c" "router/sys_watch_router.c"
    INCLUDE_DIRS "." "errors" "router"
    PRIV_INCLUDE_DIRS "internal"
    REQUIRES cmd errors router utils
    PRIV_REQUIRES board_id crc data_matrix sys_event sys_session router_queue timer time
    WHOLE_ARCHIVE
)
