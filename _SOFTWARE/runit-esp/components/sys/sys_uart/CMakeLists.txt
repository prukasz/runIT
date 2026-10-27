idf_component_register(
    SRCS "sys_uart.c"
    INCLUDE_DIRS "." "errors"
    REQUIRES errors
    PRIV_REQUIRES router sys_data_connector utils esp_driver_uart esp_driver_usb_serial_jtag
)
