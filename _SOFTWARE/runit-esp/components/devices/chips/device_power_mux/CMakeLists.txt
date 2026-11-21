include("${CMAKE_CURRENT_LIST_DIR}/../../device.cmake")
runit_device(
    SRCS "device_power_mux.c"
    PRIV_REQUIRES errors cmd utils sys_device sys_io sys_power
)
