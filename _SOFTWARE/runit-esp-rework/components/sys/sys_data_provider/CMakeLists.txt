# Providers: the ways bytes go out and come in, by id, each one a stream port of the data matrix
# (SYS_DATA_PROVIDER.MD); their router. WHOLE_ARCHIVE: the router registers itself (ROUTER_MODULE_REGISTER) from a source
# nothing else references, and the linker would drop the registration.
idf_component_register(
    SRCS "sys_data_provider.c" "router/sys_data_provider_router.c"
    INCLUDE_DIRS "." "driver" "errors" "events" "router"
    PRIV_INCLUDE_DIRS "internal"
    REQUIRES errors utils cmd router sys_event data_matrix
    WHOLE_ARCHIVE
)
