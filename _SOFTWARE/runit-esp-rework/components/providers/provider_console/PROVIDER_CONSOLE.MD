# provider_console — board UART + USB-JTAG console provider

Component `runit-esp-rework/components/providers/provider_console/`. Self-registering singleton provider driver (`SYS_DATA_PROVIDER_TYPE_CONSOLE = 4`) exposing the board's **console** (`CONFIG_ESP_CONSOLE_UART_NUM` UART0 on GPIO 43/44 at `CONFIG_ESP_CONSOLE_UART_BAUDRATE`, plus USB-Serial-JTAG when enabled) to `sys_data_provider` via `runit_provider()` (`WHOLE_ARCHIVE`, zero public header, zero `INCLUDE_DIRS`).

The console is marked `STATE_WIRED | STATE_FRAMED | STATE_UP` (plus `STATE_SYSTEM` when created with `SYS_DATA_PROVIDER_CREATE_SYSTEM` at boot). Hardware UART ports (the way to a slave among them) are `CONTRACT_UART` devices (`device_esp32s3_uart`, type `0x004D`) bridged to `sys_data_provider` by `components/providers/provider_uart/` (`PROVIDER_UART.MD`).

## Files

| File | Role |
|---|---|
| `provider_console.c` | Self-registers `SYS_DATA_PROVIDER_TYPE_CONSOLE`, installs UART0 + USB-Serial-JTAG console drivers in `console_create()`, runs static RX/TX tasks on the line of `sys_uart_codec.h` |
| `CMakeLists.txt` | Uses `components/providers/provider.cmake` (`runit_provider()`) with `PRIV_REQUIRES cmd errors sys_data_provider sys_uart_contract utils esp_driver_uart esp_driver_usb_serial_jtag` |
| `Kconfig` | Console RX/TX tasks, TX queue, driver RX buffer, send wait (`CONFIG_PROVIDER_CONSOLE_*`) |
| `AGENTS.md` | Rules for changing this folder |

## Lifecycle and line format

1. Created once at boot in `app/runit` (`connectivity/runit_ways.c`) by `sys_data_provider_create_system` with `type = SYS_DATA_PROVIDER_TYPE_CONSOLE` (`4`) and a 0-byte cfg — **before** creating `device_esp32s3_gpio` so ESP-IDF reserves the console UART pins before GPIO scans `esp_gpio_is_reserved()`.
2. `console_create()` installs UART0 (and moves `stdout` to its VFS driver) and USB-Serial-JTAG if enabled, makes its TX queue (`sys_uart_txq_t`, once), starts `tx_task` and `rx_task`, and sets `p->send = provider_send` (`ERR_PROV_IN_USE` on a second create while active).
3. **The line** is a FRAMED UART's (`SYS_UART_CONTRACT.MD` "The line", `sys_uart_codec.h`): `00 cobs([frame][crc16 LE]) 00`.
   - **RX**: bytes up to each `0x00` form a candidate, `sys_uart_unframe` decodes it and checks its CRC, and the frame goes whole to `sys_data_provider_deliver()`. Boot ROM text, panics, or logs are dropped without a word, so host tools print them as text.
   - **TX**: `provider_send` pushes the frame into the TX queue (`sys_uart_txq_push`: `CMD_SEND_URGENT` first, `CMD_SEND_SPARE` in half of it); without `CMD_SEND_NOWAIT` it waits up to `CONFIG_PROVIDER_CONSOLE_SEND_WAIT_MS`, else `ERR_PROV_BUSY`.
   - **Two outputs**: `tx_task` writes to UART0 (skipping frames that do not fit `uart_get_tx_buffer_free_size` so slow UART never stalls USB) and, when connected, to USB-Serial-JTAG in chunks of `USB_JTAG_TX_BUF / 2`.

## Limits & Open

- Largest frame `CMD_FRAME_MAX`; on the line at most `CMD_FRAME_MAX + 2` (CRC) + `3` (COBS) + `2` (`0x00` delimiters).
- `CONFIG_PROVIDER_CONSOLE_TX_QUEUE` (8 KB) plus an urgent queue of two max-size frames, on the heap, made once.
- **Interrupt-driven console RX**: the console RX task still polls `uart_read_bytes(..., 10 ms)` and `usb_serial_jtag_read_bytes(..., 10 ms)` (`device_esp32s3_uart` already uses UART ISR events).

## Tests

`python runit-esp-rework/tools/session_client.py <COM port>` opens a session over the console (`app/runit` attaches it) and runs PING and PEERS (`SYS_SESSION.MD` "Tests").
