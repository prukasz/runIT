runit_device(
    SRCS "device_esp32s3_uart.c"
    PRIV_REQUIRES errors utils cmd sys_device sys_io_contract sys_event sys_uart_contract esp_driver_uart esp_driver_gpio esp_ringbuf
)
