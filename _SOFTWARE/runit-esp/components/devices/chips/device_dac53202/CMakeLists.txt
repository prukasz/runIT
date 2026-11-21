include("${CMAKE_CURRENT_LIST_DIR}/../../device.cmake")
runit_device(
    SRCS "device_dac53202.c"
    PRIV_REQUIRES errors cmd sys_device sys_io sys_i2c
)
