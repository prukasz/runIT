# WHOLE_ARCHIVE: the error tags are registered by a constructor in errors/sys_session_errors.c, which nothing else
# references: the linker would drop the registration.
idf_component_register(
    SRCS "sys_session.c" "sys_session_route.c" "sys_session_call.c" "router/sys_session_router.c"
         "errors/sys_session_errors.c"
    INCLUDE_DIRS "." "config" "frame" "errors" "router"
    REQUIRES cmd errors utils router sys_data_provider
    PRIV_REQUIRES nvs_flash esp_timer
    WHOLE_ARCHIVE
)
