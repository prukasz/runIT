include("${CMAKE_CURRENT_LIST_DIR}/../../device.cmake")
runit_device(
    SRCS "device_tca6424a.c"
    PRIV_REQUIRES errors utils cmd sys_device sys_device_event_contract sys_io_contract sys_i2c_contract sys_event
)
