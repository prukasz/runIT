# sys_uart

Spec: `SYS_UART.MD` (this folder).

- Every change in this folder is followed by an edit of this file, if it is needed.
- The provider's id `SYS_UART_PROVIDER` (`SYS_DATA_PROVIDER_LINK_UART`, the console's port) and its state (framed, wired) are wire format: the app and the scripts name it.
- The line format is wire format: `00 cobs([frame][crc16 LE]) 00`, CRC16-CCITT (poly 0x1021, init 0xFFFF). `router_link.py` (the scripts) encodes and decodes the same: change both together.
- Static buffers: the candidate buffers (`s_uart_rx`, `s_usb_rx`), the decoded frame (`s_frame`) and the TX task's encode buffers are static, sized for `CMD_FRAME_MAX` (`cmd.h`).
- No error tags of its own: a send's failure is the provider's (`ERR_PROV_BUSY`, raised here). `ERR_MOD_SYS_UART` (3) stays unused.
- TX: the provider's `send` only queues (normal or urgent ring) and notifies the TX task; only the TX task writes frames. A write to the UART never blocks on its baud rate (the driver's TX buffer; a frame that does not fit is not written there); the USB-Serial-JTAG gets pieces of at most half its TX buffer.
- RX: a candidate that is no frame is dropped silently (no report: the ROM's text and a panic are no frames). Valid frames go to the provider's user (`sys_data_provider_deliver`).
- Initialization: `sys_uart_init` installs the console drivers, redirects `stdout` VFS to the UART driver, adds the provider and launches the TX and RX tasks; the session attaches it afterwards (`runit.c`).
