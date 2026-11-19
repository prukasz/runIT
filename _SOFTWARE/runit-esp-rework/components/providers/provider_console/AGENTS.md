# provider_console

Spec: `PROVIDER_CONSOLE.MD` (this folder).

- Every change in this folder is followed by an edit of `PROVIDER_CONSOLE.MD`, if it is needed.
- The line format (`00 cobs([frame][crc16 LE]) 00`, CRC16-CCITT poly `0x1021` init `0xFFFF`) is wire format: `runit-esp-rework/tools/session_client.py` and host scripts encode and decode the same.
- Singleton console provider (`SYS_DATA_PROVIDER_TYPE_CONSOLE = 4`, `UART0` + `USB_SERIAL_JTAG`), its line all static (`U_*_DEFINE`, `s_console`): no task created or deleted after the first create, the line stays after a remove; the provider itself (`base`, `rx_flags`) is in the registry's slot. One public header, `provider_console.h`: the CREATE cfg (`{u8 rx_flags}`, 1 B, wire format), nothing else. Must be created at boot before `device_esp32s3_gpio` so ESP-IDF reserves the console UART pins first. Hardware UART ports (including the board link) live in `device_esp32s3_uart` and are bridged by `provider_uart`.
- A flat `cmd_in_t` frame is queued as `[frame][crc16]` (`sys_uart_txq_push`; `SPARE` measured with `vRingbufferGetInfo`, since `xRingbufferGetCurFreeSize` caps `NOSPLIT` rings at `cap / 2 - 8`).
- Console TX never blocks on UART0's baud rate (`uart_get_tx_buffer_free_size`), and USB-Serial-JTAG writes chunks of at most half its TX buffer.
- Invalid RX candidates (ROM text, panics, logs) are dropped silently; valid frames go out of the provider's port under `s_rx_lock`, which `console_remove` takes to forget the provider.
