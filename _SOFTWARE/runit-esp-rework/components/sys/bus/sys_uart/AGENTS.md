# sys_uart

Spec: `SYS_UART.MD` (this folder).

- Every change in this folder is followed by an edit of this file, if it is needed.
- The providers' state (wired, framed), `sys_uart_mode_e` and the baud limits are wire format: the app and the scripts name them. Never renumber.
- The line format is wire format: `00 cobs([frame][crc16 LE]) 00`, CRC16-CCITT (poly 0x1021, init 0xFFFF). `runit-esp-rework/tools/session_client.py` and the host scripts (`router_link.py`) encode and decode the same: change them together.
- One UART is one provider, with one reader (its rx hook). Never a second provider on a UART.
- A frame is queued as one item `[head][body][crc16]`: the CRC is carried over both parts (`crc16(crc16(init, head), body)`), never computed on a joined copy. `SPARE` uses `sys_data_provider_spare_refused` with the ring's own size, never a threshold of its own.
- Only the board's ports live here (the console, the link), all static (`U_*_DEFINE`): no heap, no task made or deleted after init. A UART made at run time is a device (`device_esp32s3_uart`, SYS_UART.MD "Open"), not a port of this component.
- The link's tasks are never deleted: lent, they park (`park_tasks`) and are woken (`wake_tasks`); a static TCB is never started twice. While lent the provider is down and `provider_send` refuses a send that came in before, so nothing writes to its rings.
- The link's pins come from Kconfig and are reserved in ESP-IDF (`esp_gpio_reserve`) at `sys_uart_init`, which runs before the GPIO device is made. Its rate is the console's: no option of its own.
- Only sys_flasher lends the link, on one task. A DIRECT port is read and written only by its owner, by the id, on its own task.
- Errors are `ERR_MOD_SYS_UART` (`MODULE_ID_UART` 0x13); a send's failure is the provider's (`ERR_PROV_BUSY`, `ERR_PROV_DOWN`, raised here). Each tag has its text in `errors/errors_sys_uart.json`.
- TX: the provider's `send` only queues (normal or urgent ring) and notifies the TX task; only the TX task writes frames. The console's write to the UART never blocks on its baud rate (the driver's TX buffer; a frame that does not fit is not written there); the link's driver has no TX buffer, its TX task writes to the FIFO and waits; the USB-Serial-JTAG gets pieces of at most half its TX buffer.
- RX: a candidate that is no frame is dropped silently (no report: the ROM's text and a panic are no frames). Valid frames go to the provider's user (`sys_data_provider_deliver`).
