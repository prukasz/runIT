# sys_uart

Spec: `SYS_UART.MD` (this folder).

- Every change in this folder is followed by an edit of this file, if it is needed.
- The provider's id `SYS_UART_PROVIDER` (`SYS_DATA_PROVIDER_LINK_UART`, the console's port) and its state (framed, wired) are wire format: the app and the scripts name it.
- The line format is wire format: `00 cobs([frame][crc16 LE]) 00`, CRC16-CCITT (poly 0x1021, init 0xFFFF). `router_link.py` (the scripts) encodes and decodes the same: change both together.
- Static buffers: each port (`port_t`: the console's, the link's) has its candidate buffers, its decoded frame and its TX task's encode buffers, sized for `CMD_FRAME_MAX` (`cmd.h`). The ports share nothing but the COBS and CRC functions; a task takes its port as its argument.
- The link (`CONFIG_SYS_UART_LINK`) is a port with no USB output and a TX task that waits for room; its pins and rate are Kconfig, the same on both boards. Its pins must stay clear of the console's, the USB's (19/20), the strapping pins (0, 3, 45, 46) and the board's I2C and expander pins.
- No error tags of its own: a send's failure is the provider's (`ERR_PROV_BUSY`, raised here). `ERR_MOD_SYS_UART` (3) stays unused.
- TX: the provider's `send` only queues (normal or urgent ring) and notifies the TX task; only the TX task writes frames. A write to the UART never blocks on its baud rate (the driver's TX buffer; a frame that does not fit is not written there); the USB-Serial-JTAG gets pieces of at most half its TX buffer.
- RX: a candidate that is no frame is dropped silently (no report: the ROM's text and a panic are no frames). Valid frames go to the provider's user (`sys_data_provider_deliver`).
- Initialization: `sys_uart_init` installs the console drivers, redirects `stdout` VFS to the UART driver, adds the provider and launches the TX and RX tasks; the session attaches it afterwards (`runit.c`).
