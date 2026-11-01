# WHOLE_ARCHIVE: the error tags are registered by a constructor in errors/sys_watch_errors.c, which nothing else
# references: the linker would drop the registration.
idf_component_register(
    SRCS "sys_watch.c" "router/sys_watch_router.c" "errors/sys_watch_errors.c"
    INCLUDE_DIRS "." "config" "errors" "router"
    REQUIRES cmd errors utils sys_event
    PRIV_REQUIRES router sys_data_connector sys_session esp_timer
    WHOLE_ARCHIVE
)
