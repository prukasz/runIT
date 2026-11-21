include("${CMAKE_CURRENT_LIST_DIR}/../../device.cmake")
runit_device(
    SRCS "device_ap33772s.c"
    PRIV_REQUIRES errors cmd utils sys_device sys_io sys_i2c sys_power
)
