# The whole device is one source: nothing includes it (no INCLUDE_DIRS). WHOLE_ARCHIVE: the type registers itself
# (SYS_DEVICE_TYPE_DEFINE) and nothing references it.
idf_component_register(
    SRCS "device_dc_motor.c"
    PRIV_REQUIRES errors cmd sys_device sys_event sys_hbridge sys_device_timer time
    WHOLE_ARCHIVE
)
