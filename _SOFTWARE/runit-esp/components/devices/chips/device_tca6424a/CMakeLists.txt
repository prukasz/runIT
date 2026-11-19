include("${CMAKE_CURRENT_LIST_DIR}/../../device.cmake")
runit_device(
    SRCS "device_tca6424a.c"
    PRIV_REQUIRES errors utils cmd sys_device sys_device_event sys_io sys_i2c sys_event
)
