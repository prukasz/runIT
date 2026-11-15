include("${CMAKE_CURRENT_LIST_DIR}/../provider.cmake")
runit_provider(
    SRCS "provider_console.c"
    PRIV_REQUIRES cmd errors sys_data_provider sys_uart_contract utils esp_driver_uart esp_driver_usb_serial_jtag
)
