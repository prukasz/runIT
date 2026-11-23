include("${CMAKE_CURRENT_LIST_DIR}/../../device.cmake")
runit_device(
    SRCS "device_spi_lcd.c"
    PRIV_REQUIRES errors cmd sys_device sys_display sys_spi sys_io
)
