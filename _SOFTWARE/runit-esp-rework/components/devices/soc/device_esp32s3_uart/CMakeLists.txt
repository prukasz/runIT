runit_device(
    SRCS "device_esp32s3_uart.c" "esp32s3_uart_slot.c"
    PRIV_REQUIRES errors utils cmd sys_device sys_io sys_event sys_uart_contract esp_driver_uart esp_driver_gpio
)
