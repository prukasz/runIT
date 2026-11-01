# WHOLE_ARCHIVE: the error tags are registered by a constructor in errors/sys_data_connector_errors.c, which nothing else
# references: the linker would drop the registration.
idf_component_register(
    SRCS "sys_data_connector.c" "router/sys_data_connector_router.c" "errors/sys_data_connector_errors.c"
    INCLUDE_DIRS "." "config" "errors" "router"
    PRIV_INCLUDE_DIRS "internal"
    REQUIRES errors utils sys_data_provider
    PRIV_REQUIRES cmd router sys_session log esp_ringbuf
    WHOLE_ARCHIVE
)
