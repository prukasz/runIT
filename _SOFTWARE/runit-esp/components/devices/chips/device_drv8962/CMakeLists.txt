include("${CMAKE_CURRENT_LIST_DIR}/../../device.cmake")
runit_device(
    SRCS "device_drv8962.c"
    PRIV_REQUIRES errors cmd utils time sys_device sys_device_event sys_event sys_io sys_hbridge sys_power
)
