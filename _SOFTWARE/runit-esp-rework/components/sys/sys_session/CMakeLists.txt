# Sessions: the board's ways to its neighbours and the clients (SYS_SESSION.MD). WHOLE_ARCHIVE: the router registers
# itself (ROUTER_MODULE_REGISTER) from a source nothing else references.
idf_component_register(
    SRCS "sys_session.c" "call/sys_session_call.c" "board/sys_session_board.c" "router/sys_session_router.c"
    INCLUDE_DIRS "." "errors" "router"
    PRIV_INCLUDE_DIRS "internal"
    REQUIRES cmd errors utils router router_queue sys_data_provider
    PRIV_REQUIRES timer time nvs_flash esp_app_format
    WHOLE_ARCHIVE
)
