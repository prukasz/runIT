include("${CMAKE_CURRENT_LIST_DIR}/../../device.cmake")
runit_device(
    SRCS "device_servo.c"
    PRIV_REQUIRES errors cmd sys_device sys_io
)
