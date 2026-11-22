include("${CMAKE_CURRENT_LIST_DIR}/../../device.cmake")
runit_device(
    SRCS "device_esp32_spi.c"
    PRIV_REQUIRES errors cmd sys_device sys_spi sys_io esp_driver_spi
)
