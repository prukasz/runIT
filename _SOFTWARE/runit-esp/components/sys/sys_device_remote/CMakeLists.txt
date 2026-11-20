# Devices of other boards (SYS_DEVICE_REMOTE.MD). WHOLE_ARCHIVE: the router registers itself (ROUTER_MODULE_REGISTER) and the
# error tags by a constructor, from sources nothing else references.
idf_component_register(
    SRCS "sys_device_remote.c" "router/sys_device_remote_router.c"
    INCLUDE_DIRS "." "config" "errors" "router"
    REQUIRES cmd errors router
    PRIV_REQUIRES data_matrix router_queue sys_session sys_device sys_device_router sys_watch sys_event utils
                  esp_ringbuf
    WHOLE_ARCHIVE
)
