include("${CMAKE_CURRENT_LIST_DIR}/../../device.cmake")
runit_device(
    SRCS "device_ina3221.c"
    PRIV_REQUIRES errors cmd sys_device sys_device_event_contract sys_event sys_io_contract sys_i2c_contract sys_power_contract
)
