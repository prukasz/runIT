# WHOLE_ARCHIVE: the type registers itself (SYS_DEVICE_TYPE_DEFINE) and nothing else references it.
idf_component_register(
    SRCS "device_drv8962.c"
    INCLUDE_DIRS "."
    REQUIRES errors utils cmd sys_device sys_io sys_event sys_hbridge sys_power
    PRIV_REQUIRES esp_rom
    WHOLE_ARCHIVE
)
