# sys_uart — the board's UART console as a provider

Component `runit-esp-rework/components/sys/bus/sys_uart/`. Exposes the board's **console** as a `sys_data_provider`:

| Port | Provider | Made by | Mode |
|---|---|---|---|
| **The console's** (`CONFIG_ESP_CONSOLE_UART_NUM` UART0 on GPIO 43/44 at `CONFIG_ESP_CONSOLE_UART_BAUDRATE`, plus USB-Serial-JTAG when enabled) | what the board passes to `sys_uart_init` | `sys_uart_init`, always | framed |

The console is marked `STATE_SYSTEM | STATE_WIRED | STATE_FRAMED | STATE_UP`. Other UART ports — including the board-to-board link (`CONFIG_BOARD_UART_LINK`) — are `CONTRACT_UART` devices (`device_esp32s3_uart`, type `0x004D`) bridged to `sys_data_provider` by `components/providers/provider_uart/` (`PROVIDER_UART.MD`).

## Files

| File | Role |
|---|---|
| `sys_uart.h` | API: `sys_uart_init(console_provider)` |
| `sys_uart.c` | Console UART + USB-Serial-JTAG drivers, static rings, RX/TX tasks using `sys_uart_codec.h`, self-registers `SYS_DATA_PROVIDER_TYPE_CONSOLE` |
| `errors/`, enums | Live in `sys_uart_contract` (`MODULE_ID_UART` 0x13, `ERR_MOD_SYS_UART`) |
| `CMakeLists.txt` | Requires `errors`, `sys_data_provider`, `sys_uart_contract` |
| `Kconfig` | Console RX/TX tasks, TX queue, driver RX buffer, send wait |
| `AGENTS.md` | Rules for changing this folder |

## The line

**Binary**: every frame goes COBS-encoded with its CRC16, between `0x00` bytes: `00 cobs([frame][crc16 LE]) 00`, CRC16-CCITT (poly `0x1021`, init `0xFFFF`, no reflection, no final xor). COBS leaves no other `0x00`, so a frame ends at the next one; COBS adds at most one byte in 254.

- **RX**: bytes up to each `0x00` form a candidate, COBS-decoded in its own static buffer, CRC checked, and delivered whole to `sys_data_provider_deliver`. Boot ROM text, panics, or logs that fail CRC are dropped silently so host tools can print them as text.
- **TX**: `provider_send` receives `[head]` and `[body]` (`CMD_FRAME_MAX` max) and queues `[head][body][crc16]` as one item (`CMD_SEND_URGENT` uses its own urgent ring; `CMD_SEND_SPARE` uses `sys_data_provider_spare_refused` against `ring_used` via `vRingbufferGetInfo`). Without `CMD_SEND_NOWAIT`, it waits up to `CONFIG_SYS_UART_SEND_WAIT_MS`, else `ERR_PROV_BUSY`.
- **Two outputs**: the console TX task writes to UART0 (skipping frames that do not fit `uart_get_tx_buffer_free_size` so slow UART never stalls USB) and, when connected, to USB-Serial-JTAG in chunks of `USB_JTAG_TX_BUF / 2`.
- **Initialization** (`sys_uart_init`): installs UART0 (and moves `stdout` to its VFS driver) and USB-Serial-JTAG if enabled, starts the static TX and RX tasks, and registers the console provider. Run once at boot before creating `device_esp32s3_gpio` so ESP-IDF's console/USB pins stay reserved.

## Limits

- Largest frame `CMD_FRAME_MAX`; on the line at most `CMD_FRAME_MAX` + 2 (CRC) + 3 (COBS) + 2 (`0x00` delimiters).
- `CONFIG_SYS_UART_TX_QUEUE` (8 KB) plus an urgent queue of two max-size frames; all static.

## Tests

`python runit-esp-rework/tools/session_client.py <COM port>` opens a session over the console (`main` attaches it `UP`) and runs PING and PEERS (`SYS_SESSION.MD` "Tests").

## Open

- **`DIRECT` mode for `sys_flasher` (Stage 6)**: lending the board link for ROM bootloader flashing (`sys_uart_link_direct`, `sys_uart_write`, `sys_uart_read`, `sys_uart_flush_input`, `sys_uart_set_baud`) will be restored in Stage 6 on top of the `device_esp32s3_uart` + `provider_uart` architecture.
- **Interrupt-driven console RX**: the console RX task still polls `uart_read_bytes(..., 10 ms)` and `usb_serial_jtag_read_bytes(..., 10 ms)` (`device_esp32s3_uart` already uses UART ISR events).
