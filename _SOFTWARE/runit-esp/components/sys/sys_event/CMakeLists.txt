idf_component_register(
    SRCS "sys_event.c"
    INCLUDE_DIRS "."
    REQUIRES errors utils cmd sys_device
    PRIV_REQUIRES sys_connector router esp_timer
)
