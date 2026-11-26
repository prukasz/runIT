include("${CMAKE_CURRENT_LIST_DIR}/../../device.cmake")
runit_device(
    SRCS "device_pca9685.c"
    PRIV_REQUIRES errors cmd sys_device sys_io_contract sys_i2c_contract time
)
