# Stored command lists (SYS_ACTIONS.MD): records in NVS, replayed through the router. WHOLE_ARCHIVE: the router
# registers itself (ROUTER_MODULE_REGISTER) from a source nothing else references.
idf_component_register(
    SRCS "sys_actions.c" "router/sys_actions_router.c"
    INCLUDE_DIRS "." "errors" "router"
    REQUIRES errors cmd router
    PRIV_REQUIRES router_queue utils nvs_store esp_rom sys_device sys_system
    WHOLE_ARCHIVE
)
