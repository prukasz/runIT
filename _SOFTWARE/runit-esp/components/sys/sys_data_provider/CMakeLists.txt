# WHOLE_ARCHIVE: the error tags are registered by a constructor in errors/sys_data_provider_errors.c, which nothing else
# references: the linker would drop the registration.
idf_component_register(
    SRCS "sys_data_provider.c" "router/sys_data_provider_router.c" "errors/sys_data_provider_errors.c"
    INCLUDE_DIRS "." "config" "driver" "errors" "router"
    PRIV_INCLUDE_DIRS "internal"
    REQUIRES errors utils cmd router
    WHOLE_ARCHIVE
)
