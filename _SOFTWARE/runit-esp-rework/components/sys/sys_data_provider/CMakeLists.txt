# Providers: the ways bytes go out and come in, by id, one owner of the rx hook each (SYS_DATA_PROVIDER.MD); their
# command binding and their router. WHOLE_ARCHIVE: the router registers itself (ROUTER_MODULE_REGISTER) from a source
# nothing else references, and the linker would drop the registration.
idf_component_register(
    SRCS "sys_data_provider.c" "sys_data_provider_cmd.c" "router/sys_data_provider_router.c"
    INCLUDE_DIRS "." "driver" "errors" "router"
    PRIV_INCLUDE_DIRS "internal"
    REQUIRES errors utils cmd router
    PRIV_REQUIRES router_queue
    WHOLE_ARCHIVE
)
