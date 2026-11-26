include("${CMAKE_CURRENT_LIST_DIR}/../../device.cmake")
runit_device(
    SRCS "device_esp32s3_i2c.c"
    PRIV_REQUIRES errors cmd sys_device sys_i2c_contract sys_io_contract esp_driver_i2c
)
