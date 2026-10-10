# SYS_UART — Console UART as a Data Connector Provider

IMPORTANT: When changing this component, update this document to match its API and behavior.

`sys_uart` makes the console UART (GPIO 43/44 through the board's USB-UART bridge, 115200 baud) a `sys_data_connector` provider ([[SYS_DATA_CONNECTOR.MD]]). The same wire carries the text log, so frames travel as text lines that a reader picks out of the log:

```
#R:<hex bytes>\n
```

- **TX:** uppercase hex, one line per frame (stream byte included), written through `stdout` under its lock, so a log line from another task can't land inside it.
- **RX:** either case. Lines that don't start with `#R:` are ignored, so typing in a monitor does nothing. A line with an odd digit count, a non-hex character or more than `2 × CONFIG_SYS_DATA_CONNECTOR_FRAME_MAX` digits is dropped and reported (`ERR_UART_LINE_BAD`).

When the native USB (USB-Serial-JTAG) is a console output too (`CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG` or `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG`), it gets the same `stdout` lines, and the RX task also reads `#R:` lines from it (USB-Serial-JTAG driver installed with an RX buffer; TX still goes through `stdout`). Each input has its own line assembler; both feed the same frame queue. This makes the runIT PCB's only USB port a full command link.

It is the command link the host test scripts use (`.claude/skills/runit-esp/scripts/runit_link.py`), and a fallback when BLE is unavailable.

## Files

- `include/sys_uart_provider.h`: `sys_uart_provider_register()`, `SYS_UART_ENDPOINT_CONSOLE`.
- `sys_uart_provider.c`: provider ops, RX task (line de-framing), registration.
- `errors/sys_error_uart.h`: owners (`0xB100`–`0xB103`) and tags (aggregated by `sys_errors`).
- `Kconfig` ("UART Console Link"): RX task priority / stack, UART driver RX buffer, decoded frame queue size.

## Provider

| Op | Behaviour |
|---|---|
| `send` | Writes one `#R:` line. Endpoint must be `0`. |
| `dequeue` | Pops one decoded frame from a static ring buffer (`CONFIG_SYS_UART_RX_QUEUE_SIZE`). Peer is always 0. A frame longer than the caller's buffer is dropped: `ERR_UART_FRAME_TOO_LONG`. |
| `max_frame` | Not set: the limit is `CONFIG_SYS_DATA_CONNECTOR_FRAME_MAX`. |
| `bind_rx` / `unbind_rx` | One connector at a time (`ERR_BASE_INVALID_STATE` for a second one). Binding wakes the consumer when frames are already waiting. |

`sys_uart_provider_register(id)` installs the UART driver on the console UART (RX buffer only; `stdout` is moved to the driver with `uart_vfs_dev_use_driver`), starts the RX task and registers the provider. Called once at boot.

## Board wiring (runit)

With `CONFIG_RUNIT_UART_CONSOLE_LINK=y` (default), `runit_board_connector_bindings_init()` registers the provider as `RUNIT_DATA_PROVIDER_UART` (`CONFIG_SYS_DATA_PROVIDER_ID_UART`, 2) and binds, next to BLE:

| Connector | Direction |
|---|---|
| errors (`0x04`) | TX |
| telemetry (`0x02`) | TX |
| interface (`0x05`) | TX + RX |

The logs connector is not bound: the text log is already on the console (serial mirror). Responses go only to the command's origin, so a command sent over UART is answered on UART and one sent over BLE on BLE.

## Errors

| Tag | Level | Payload | Raised when |
|---|---|---|---|
| `ERR_UART_LINE_BAD` (`0xB101`) | LOW | `uint16_t len;` | A `#R:` line is malformed or too long (RX task, `SE_RAISE`). |
| `ERR_UART_RX_FULL` (`0xB102`) | MEDIUM | `uint16_t len;` | The decoded frame queue is full; the frame is dropped. |
| `ERR_UART_FRAME_TOO_LONG` (`0xB103`) | MEDIUM | `uint16_t len; uint16_t max;` | `dequeue` into a smaller buffer. |

Identical chains inside `CONFIG_SYS_ERRORS_REPEAT_WINDOW_MS` are sent once and counted ([[SYS_ERRORS.MD]]), so a burst of bad lines shows as one error plus a "repeated N more time(s)" log line.
