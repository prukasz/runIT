# The whole device is one source: nothing includes it (no INCLUDE_DIRS). WHOLE_ARCHIVE: the type registers itself
# (SYS_DEVICE_TYPE_DEFINE) and nothing references it.
idf_component_register(
    SRCS "device_tps55289.c"
    PRIV_REQUIRES errors time utils sys_device sys_device_event sys_device_timer sys_io sys_i2c sys_event sys_power
    WHOLE_ARCHIVE
)
