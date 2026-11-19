# Providers: the ways bytes go out and come in, by id, each one a stream port of the data matrix
# (SYS_DATA_PROVIDER.MD), and their router. WHOLE_ARCHIVE: the router registers itself (ROUTER_MODULE_REGISTER), and a
# board with no provider driver references nothing else of this source: the linker would drop the registration.
idf_component_register(
    SRCS "sys_data_provider.c"
    INCLUDE_DIRS "." "driver" "errors" "events" "router"
    REQUIRES errors utils cmd router sys_event data_matrix
    WHOLE_ARCHIVE
)
