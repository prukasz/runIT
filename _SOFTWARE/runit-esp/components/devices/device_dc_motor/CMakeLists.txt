# WHOLE_ARCHIVE: the type registers itself (SYS_DEVICE_TYPE_REGISTER) and nothing else references it.
idf_component_register(
    SRCS "device_dc_motor.c"
    INCLUDE_DIRS "."
    REQUIRES errors cmd sys_device sys_event sys_hbridge
    PRIV_REQUIRES esp_timer
    WHOLE_ARCHIVE
)
