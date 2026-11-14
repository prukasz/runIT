include("${CMAKE_CURRENT_LIST_DIR}/../../device.cmake")
runit_device(
    SRCS "device_tps55289.c"
    PRIV_REQUIRES errors time utils sys_device sys_device_event sys_device_timer sys_io sys_i2c sys_event sys_power
)
