include("${CMAKE_CURRENT_LIST_DIR}/../../device.cmake")
runit_device(
    SRCS "device_ina3221.c"
    PRIV_REQUIRES errors cmd sys_device sys_device_event sys_event sys_io sys_i2c sys_power
)
