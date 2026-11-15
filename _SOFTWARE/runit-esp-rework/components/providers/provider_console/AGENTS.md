# provider_console

Spec: `PROVIDER_CONSOLE.MD` (this folder).

- Every change in this folder is followed by an edit of `PROVIDER_CONSOLE.MD`, if it is needed.
- The line format (`00 cobs([frame][crc16 LE]) 00`, CRC16-CCITT poly `0x1021` init `0xFFFF`) is wire format: `runit-esp-rework/tools/session_client.py` and host scripts encode and decode the same.
- Singleton console provider (`SYS_DATA_PROVIDER_TYPE_CONSOLE = 4`, `UART0` + `USB_SERIAL_JTAG`), all static (`U_*_DEFINE`): no heap, no task created or deleted after first create, zero public header (`runit_provider()`). Must be created at boot before `device_esp32s3_gpio` so ESP-IDF reserves the console UART pins first. Hardware UART ports (including the board link) live in `device_esp32s3_uart` and are bridged by `provider_uart`.
- A flat `cmd_in_t` frame is queued as `[frame][crc16]`; `SPARE` uses `sys_data_provider_spare_refused` with `ring_used` computed via `vRingbufferGetInfo` (`xRingbufferGetCurFreeSize` caps `NOSPLIT` rings at `cap / 2 - 8`).
- Console TX never blocks on UART0's baud rate (`uart_get_tx_buffer_free_size`), and USB-Serial-JTAG writes chunks of at most half its TX buffer.
- Invalid RX candidates (ROM text, panics, logs) are dropped silently; valid frames go to `sys_data_provider_deliver`.
