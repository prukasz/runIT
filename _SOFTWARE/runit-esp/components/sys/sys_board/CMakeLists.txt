# The board's profile (SYS_BOARD.MD): its stored record run at boot, and the JSON the app reads back. WHOLE_ARCHIVE:
# the router registers itself (ROUTER_MODULE_REGISTER) from a source nothing else references.
idf_component_register(
    SRCS "sys_board.c" "router/sys_board_router.c"
    INCLUDE_DIRS "." "router"
    PRIV_INCLUDE_DIRS "internal"
    REQUIRES errors cmd router nvs_store
    PRIV_REQUIRES router_queue sys_actions
    WHOLE_ARCHIVE
)
