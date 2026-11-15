# The UART contract (a port is a device) and its client (a driver on a port): sys_uart_contract.h. No port is made
# here: device_esp32s3_uart (and any bridge device) implement it, while sys_uart carries the board's console/link and
# DIRECT flasher mode. WHOLE_ARCHIVE: claims MODULE_ID_UART (0x13) and registers ERR_MOD_SYS_UART even when nothing calls
# a client function directly.
idf_component_register(
    SRCS "sys_uart_contract.c" "sys_uart_codec.c"
    INCLUDE_DIRS "." "errors"
    REQUIRES errors cmd sys_device sys_event
    WHOLE_ARCHIVE
)
