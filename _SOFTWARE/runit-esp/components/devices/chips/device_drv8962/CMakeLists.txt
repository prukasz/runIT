include("${CMAKE_CURRENT_LIST_DIR}/../../device.cmake")
runit_device(
    SRCS "device_drv8962.c"
    PRIV_REQUIRES errors cmd utils time sys_device sys_device_event_contract sys_event sys_io_contract sys_hbridge_contract sys_power_contract
)
