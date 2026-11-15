# Data streams: named streams, sources, sinks and peer subscriptions (SYS_STREAM.MD). WHOLE_ARCHIVE: the router
# registers itself (ROUTER_MODULE_REGISTER) from a source nothing else references.
idf_component_register(
    SRCS "sys_stream.c" "router/sys_stream_router.c"
    INCLUDE_DIRS "." "errors" "router"
    PRIV_INCLUDE_DIRS "internal"
    REQUIRES cmd errors router
    PRIV_REQUIRES router_queue sys_data_provider sys_session utils freertos
    WHOLE_ARCHIVE
)
