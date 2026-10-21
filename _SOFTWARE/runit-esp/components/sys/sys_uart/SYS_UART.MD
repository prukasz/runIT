# sys_uart — console UART as a router link

Component `components/sys/sys_uart/`. Makes the console UART (GPIO 43/44 via the board's USB-UART bridge, 115200 baud) and, when it is a console output, the native USB-Serial-JTAG, the router link `TRANSPORT_UART`. The same wire carries the text log, so frames travel as text lines:

```
#R:<hex bytes>\n
```

## Files

| File | Role |
|---|---|
| `sys_uart.h` | API, endpoint macro `SYS_UART_ENDPOINT` |
| `errors/sys_uart_errors.h` | `ERR_SYS_UART_MAP`, the module's error tags (included by `sys_uart.h`) |
| `errors/errors_sys_uart.json` | The error messages |
| `sys_uart.c` | Line framing, RX task, router link and connector provider |
| `Kconfig` | RX task priority, stack size, driver RX buffer |
| `AGENTS.md` | Rules for changing this folder |

## Operation

- **RX**: lines starting with `#R:` (hex in either case) → `router_submit(TRANSPORT_UART, s_frame, size)`. Other lines are ignored (typing in a monitor does nothing). Odd digit count, non-hex or longer than `LINE_LEN_MAX` (`LINE_PREFIX_LEN + 2 * CONFIG_ROUTER_FRAME_MAX`) → `ERR_UART_LINE_BAD{len}` (reported). Each input (UART, USB-Serial-JTAG) has its own line assembler.
- **TX** (`send`): one uppercase `#R:` line per response, written under the `stdout` lock (`flockfile` / `funlockfile`) so a log line cannot land inside it; safe from both router tasks. Peer always 0, frame limit `CONFIG_ROUTER_FRAME_MAX`.
- **Connector provider** (`TRANSPORT_UART`, endpoint `SYS_UART_ENDPOINT` = 0): outbound stream frames (errors, telemetry) go out as the same `#R:` lines; any other endpoint → `ERR_UART_BAD_ENDPOINT`. The logs connector is not bound to UART: the text log is already on the console.
- **Initialization** (`sys_uart_init`): installs the UART driver (RX buffer only; `stdout` moves to the driver) and the USB-Serial-JTAG driver, registers the link and the provider, and starts the RX task. Call after `router_init()` and `sys_connector_init()`.
- Errors on the RX task end in `err_report` (log + errors stream).

Wire format of the frames: ROUTER.MD.

## Errors (`ERR_MOD_SYS_UART` = 13)

| Error | Severity | Context / Fields | Description |
|---|---|---|---|
| `ERR_UART_LINE_BAD` | LOW | `u16 len` | `#R:` line with odd digits, non-hex characters, or exceeding maximum line length |
| `ERR_UART_BAD_ENDPOINT` | MEDIUM | `u32 endpoint` | Connector binding to an endpoint other than `SYS_UART_ENDPOINT` (0) |

## Limits

- Largest frame is bounded by `CONFIG_ROUTER_FRAME_MAX` (router link) and `CONFIG_SYS_CONNECTOR_FRAME_MAX` (connector provider).
- Single endpoint `SYS_UART_ENDPOINT` (0).
- Incoming lines without the `#R:` prefix are silently ignored as console noise.

## Tests

Host test scripts (`router_tests.py` in `.claude/skills/runit-esp/scripts/`) use `TRANSPORT_UART` over serial/USB-Serial-JTAG as the primary RPC link and stream receiver for connector bindings.
