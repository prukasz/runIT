idf_component_register(
    SRCS "sys_event.c" "sys_event_router.c"
    INCLUDE_DIRS "."
    REQUIRES errors utils cmd sys_device
    PRIV_REQUIRES router sys_connector esp_timer
)
