include("${CMAKE_CURRENT_LIST_DIR}/../../device.cmake")
runit_device(
    SRCS "device_esp32s3_i2c.c"
    PRIV_REQUIRES errors cmd sys_device sys_i2c sys_io esp_driver_i2c
)
