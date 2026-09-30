idf_component_register(
    SRCS "sys_uart_provider.c"
    INCLUDE_DIRS "include"
    REQUIRES freertos esp_ringbuf esp_driver_uart esp_driver_usb_serial_jtag sys_errors utils sys_data_connector
)
