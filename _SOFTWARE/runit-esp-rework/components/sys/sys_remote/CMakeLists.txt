# Devices of other boards (SYS_REMOTE.MD). WHOLE_ARCHIVE: the router registers itself (ROUTER_MODULE_REGISTER) and the
# error tags by a constructor, from sources nothing else references.
idf_component_register(
    SRCS "sys_remote.c" "router/sys_remote_router.c"
    INCLUDE_DIRS "." "config" "errors" "router"
    REQUIRES cmd errors router router_queue sys_session
    PRIV_REQUIRES sys_device sys_device_router sys_watch sys_event utils esp_ringbuf
    WHOLE_ARCHIVE
)
