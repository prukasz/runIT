# The data matrix: the switch that links stream ports and hands their packets on unchanged (DATA_MATRIX.MD).
# WHOLE_ARCHIVE: the router registers itself (ROUTER_MODULE_REGISTER) from a source nothing else references.
idf_component_register(
    SRCS "data_stream.c" "router/data_stream_router.c"
    INCLUDE_DIRS "." "errors" "router"
    REQUIRES cmd errors router utils
    PRIV_REQUIRES freertos
    WHOLE_ARCHIVE
)
