# The data matrix: roles of the providers and the routes of the data that are not commands (DATA_MATRIX.MD).
# WHOLE_ARCHIVE: the router registers itself (ROUTER_MODULE_REGISTER) from a source nothing else references.
idf_component_register(
    SRCS "data_matrix.c" "data_matrix_commands.c" "router/data_matrix_router.c"
    INCLUDE_DIRS "." "errors" "router"
    PRIV_INCLUDE_DIRS "internal"
    REQUIRES cmd errors router
    PRIV_REQUIRES router_queue sys_data_provider sys_session utils freertos
    WHOLE_ARCHIVE
)
