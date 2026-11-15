# sys_uart

Spec: `SYS_UART.MD` (this folder).

- Every change in this folder is followed by an edit of this file, if it is needed.
- The provider state flags (`WIRED`, `FRAMED`) and line format (`00 cobs([frame][crc16 LE]) 00`, CRC16-CCITT poly `0x1021` init `0xFFFF`) are wire format: `runit-esp-rework/tools/session_client.py` and the host scripts encode and decode the same.
- Only the board console (`UART0` + `USB_SERIAL_JTAG`) lives here, all static (`U_*_DEFINE`): no heap, no task created or deleted after init. Hardware UART ports (including the board link) live in `device_esp32s3_uart` and are bridged to `sys_data_provider` by `provider_uart`.
- A frame is queued as one item `[head][body][crc16]`: CRC covers both parts without an intermediate join copy. `SPARE` uses `sys_data_provider_spare_refused` with `ring_used` computed via `vRingbufferGetInfo` (`xRingbufferGetCurFreeSize` caps `NOSPLIT` rings at `cap / 2 - 8`).
- Errors and enums live in `sys_uart_contract` (`ERR_MOD_SYS_UART`, `MODULE_ID_UART` 0x13, `sys_uart_contract.h`).
- Console TX never blocks on UART0's baud rate (`uart_get_tx_buffer_free_size`), and USB-Serial-JTAG writes chunks of at most half its TX buffer.
- Invalid RX candidates (ROM text, panics, logs) are dropped silently; valid frames go to `sys_data_provider_deliver`.
