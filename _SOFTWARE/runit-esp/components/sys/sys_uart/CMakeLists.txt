idf_component_register(
    SRCS "sys_uart.c"
    INCLUDE_DIRS "."
    REQUIRES errors sys_data_provider
    PRIV_REQUIRES cmd utils esp_driver_uart esp_driver_usb_serial_jtag
)
