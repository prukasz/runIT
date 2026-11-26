include("${CMAKE_CURRENT_LIST_DIR}/../../device.cmake")
runit_device(
    SRCS "device_tps55289.c"
    PRIV_REQUIRES errors time utils sys_device sys_device_event_contract sys_device_timer sys_io_contract sys_i2c_contract sys_event sys_power_contract
)
