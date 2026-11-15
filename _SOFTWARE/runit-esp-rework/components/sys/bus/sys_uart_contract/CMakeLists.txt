# The UART contract (a port is a device) and its client (a driver on a port): sys_uart_contract.h; the line's framing and
# TX queue (sys_uart_codec.h) for the ports and the console. No port is made here. WHOLE_ARCHIVE: claims MODULE_ID_UART (0x13) and registers ERR_MOD_SYS_UART even when nothing calls
# a client function directly.
idf_component_register(
    SRCS "sys_uart_contract.c" "sys_uart_codec.c"
    INCLUDE_DIRS "." "errors"
    REQUIRES errors cmd sys_device sys_event esp_ringbuf
    WHOLE_ARCHIVE
)
