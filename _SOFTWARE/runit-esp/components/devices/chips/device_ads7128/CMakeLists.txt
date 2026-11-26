include("${CMAKE_CURRENT_LIST_DIR}/../../device.cmake")
runit_device(
    SRCS "device_ads7128.c"
    PRIV_REQUIRES errors cmd utils sys_device sys_device_event_contract sys_event sys_io_contract sys_i2c_contract
)
