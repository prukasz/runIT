# WHOLE_ARCHIVE: the type registers itself (SYS_DEVICE_TYPE_REGISTER) and nothing else references it.
idf_component_register(
    SRCS "device_ads7128.c"
    INCLUDE_DIRS "."
    REQUIRES errors cmd utils sys_device sys_event sys_io sys_i2c
    WHOLE_ARCHIVE
)
