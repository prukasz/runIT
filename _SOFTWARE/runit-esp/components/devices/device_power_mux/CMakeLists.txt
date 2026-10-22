# WHOLE_ARCHIVE: the type registers itself (SYS_DEVICE_TYPE_REGISTER) and nothing else references it.
idf_component_register(
    SRCS "device_power_mux.c"
    INCLUDE_DIRS "."
    REQUIRES errors utils cmd sys_device sys_io sys_power
    WHOLE_ARCHIVE
)
