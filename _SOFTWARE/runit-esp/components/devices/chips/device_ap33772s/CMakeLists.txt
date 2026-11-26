include("${CMAKE_CURRENT_LIST_DIR}/../../device.cmake")
runit_device(
    SRCS "device_ap33772s.c"
    PRIV_REQUIRES errors cmd utils sys_device sys_device_event_contract sys_io_contract sys_i2c_contract sys_event sys_power_contract
)
