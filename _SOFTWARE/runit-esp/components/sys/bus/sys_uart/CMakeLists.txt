idf_component_register(
    SRCS "router/sys_uart_router.c" "sys_uart.c"
    INCLUDE_DIRS "." "config" "errors" "router"
    REQUIRES errors sys_data_provider
    PRIV_REQUIRES cmd router utils sys_device sys_io esp_driver_uart esp_driver_usb_serial_jtag
)
