# The whole device is one source: nothing includes it (no INCLUDE_DIRS). WHOLE_ARCHIVE: the type registers itself
# (SYS_DEVICE_TYPE_DEFINE) and nothing references it.
idf_component_register(
    SRCS "device_pca9685.c"
    PRIV_REQUIRES errors router sys_device sys_io sys_io_claim sys_i2c time
    WHOLE_ARCHIVE
)
