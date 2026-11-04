# WHOLE_ARCHIVE: the error tags are registered by a constructor in errors/sys_remote_errors.c, which nothing else
# references: the linker would drop the registration.
idf_component_register(
    SRCS "sys_remote.c" "router/sys_remote_router.c" "errors/sys_remote_errors.c"
    INCLUDE_DIRS "." "config" "errors" "router"
    REQUIRES errors cmd router
    PRIV_REQUIRES sys_device sys_session sys_watch sys_event utils esp_ringbuf
    WHOLE_ARCHIVE
)
