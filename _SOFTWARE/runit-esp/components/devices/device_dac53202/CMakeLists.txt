# WHOLE_ARCHIVE: the type registers itself (SYS_DEVICE_TYPE_REGISTER) and nothing else references it.
idf_component_register(
    SRCS "device_dac53202.c"
    INCLUDE_DIRS "."
    REQUIRES errors cmd sys_device sys_io sys_i2c
    WHOLE_ARCHIVE
)
