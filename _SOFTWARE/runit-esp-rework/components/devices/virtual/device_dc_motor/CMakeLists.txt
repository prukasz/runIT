include("${CMAKE_CURRENT_LIST_DIR}/../../device.cmake")
runit_device(
    SRCS "device_dc_motor.c"
    PRIV_REQUIRES errors cmd sys_device sys_event sys_hbridge sys_device_timer time
)
