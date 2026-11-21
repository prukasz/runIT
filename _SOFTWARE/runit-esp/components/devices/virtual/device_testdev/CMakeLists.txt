include("${CMAKE_CURRENT_LIST_DIR}/../../device.cmake")
runit_device(
    SRCS "device_testdev.c"
    PRIV_REQUIRES errors utils cmd log freertos sys_device sys_device_event sys_event sys_io sys_i2c sys_power sys_hbridge sys_uart_contract
)
