# WHOLE_ARCHIVE: the type registers itself (SYS_DEVICE_TYPE_DEFINE) and nothing else references it.
idf_component_register(
    SRCS "device_ina3221.c"
    INCLUDE_DIRS "."
    REQUIRES errors utils cmd sys_device sys_io sys_i2c sys_event sys_power
    WHOLE_ARCHIVE
)
