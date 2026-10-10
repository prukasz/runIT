# sys_uart — console UART as a router link

Component `components/sys/sys_uart/`. Makes the console UART (GPIO 43/44 via the board's USB-UART bridge, 115200 baud) and, when it is a console output, the native USB-Serial-JTAG, the router link `TRANSPORT_UART`. The same wire carries the text log, so frames travel as text lines:

```
#R:<hex bytes>\n
```

- **RX**: lines starting with `#R:` (hex in either case) → `router_submit(TRANSPORT_UART, 0, frame)`. Other lines are ignored (typing in a monitor does nothing). Odd digit count, non-hex or longer than `2 × CONFIG_ROUTER_FRAME_MAX` → `ERR_UART_LINE_BAD{len}` (logged). Each input has its own line assembler.
- **TX** (`send`): one uppercase `#R:` line per response, written under the `stdout` lock so a log line can't land inside it; safe from both router tasks. Peer always 0, frame limit `CONFIG_ROUTER_FRAME_MAX`.
- **Connector provider** (`TRANSPORT_UART`, endpoint `SYS_UART_ENDPOINT` = 0): stream frames (errors, telemetry) go out as the same `#R:` lines; any other endpoint → `ERR_UART_BAD_ENDPOINT`. The logs connector is not bound to UART: the text log is already on the console.
- `sys_uart_init()`: installs the UART driver (RX buffer only; `stdout` moves to the driver) and the USB-Serial-JTAG driver, registers the link and the provider, starts the RX task. Call after `router_init()` and `sys_connector_init()`.
- Errors on the RX task end in `err_report` (log + errors stream).
- Kconfig menu `sys_uart`: RX task priority / stack, driver RX buffer.

It is the command link of the host test scripts and a fallback when BLE is unavailable. Wire format of the frames: ROUTER.MD.

## Changes from legacy (`legacy/components/system/sys_uart`)

- A router link instead of a `sys_data_connector` provider: no own decoded-frame ring buffer (the router copies the frame into its queue), no bind/unbind.
- Errors on the new `errors` API (`ERR_MOD_SYS_UART`); `ERR_UART_RX_FULL` / `_FRAME_TOO_LONG` are gone (the router reports `ERR_ROUTER_QUEUE_FULL` / `_FRAME_LEN` and answers the frame).
