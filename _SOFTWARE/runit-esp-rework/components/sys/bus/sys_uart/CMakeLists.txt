# The board's UART ports as providers: the console and the link to another board (SYS_UART.MD).
idf_component_register(
    SRCS "sys_uart.c"
    INCLUDE_DIRS "." "config" "errors"
    REQUIRES errors sys_data_provider
    PRIV_REQUIRES cmd utils esp_driver_uart esp_driver_usb_serial_jtag esp_hw_support
)
