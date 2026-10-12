# WHOLE_ARCHIVE: the type registers itself (SYS_DEVICE_TYPE_DEFINE) and nothing else references it.
idf_component_register(
    SRCS "device_servo.c"
    INCLUDE_DIRS "."
    REQUIRES errors cmd sys_device sys_io
    WHOLE_ARCHIVE
)
