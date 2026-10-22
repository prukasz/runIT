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
| `sys_uart.c` | Line framing, RX and TX tasks, router link and connector provider |
| `Kconfig` | RX and TX tasks, the TX queue, the driver's RX buffer |
| `AGENTS.md` | Rules for changing this folder |

## Operation

- **RX**: lines starting with `#R:` (hex in either case) → `router_submit(TRANSPORT_UART, s_frame, size)`. Other lines are ignored (typing in a monitor does nothing). Odd digit count, non-hex or longer than `LINE_LEN_MAX` (`LINE_PREFIX_LEN + 2 * CONFIG_ROUTER_FRAME_MAX`) → `ERR_UART_LINE_BAD{len}` (reported). Each input (UART, USB-Serial-JTAG) has its own line assembler.
- **TX queue and task**: the link and the provider only queue the frame (`CONFIG_SYS_UART_TX_QUEUE`); the TX task (`CONFIG_SYS_UART_TX_TASK_PRIO`, low) writes each as one uppercase `#R:` line under the `stdout` lock (`flockfile` / `funlockfile`), so a log line cannot land inside it. No sender waits for the 115200 baud console, and answers keep their order with the stream frames.
- **Link** (`send`, router answers) and **provider**: a frame waits up to 1 s for room in the queue, then `ERR_UART_TX_FULL{len}`. A stream frame waiting stalls the connector task: its own queue fills and the producer is refused (the telemetry sampler sends again from the first byte), so nothing is lost here unseen; no producer waits (`sys_connector_send` never blocks). Peer always 0, frame limit `CONFIG_ROUTER_FRAME_MAX`.
- **Connector provider** (`TRANSPORT_UART`, endpoint `SYS_UART_ENDPOINT` = 0): outbound stream frames (errors, telemetry, events) go out as the same `#R:` lines; a queue still full after 1 s drops the frame (`ERR_UART_TX_FULL`, logged once by the connector task until the queue takes frames again); any other endpoint → `ERR_UART_BAD_ENDPOINT`. The logs connector is not bound to UART: the text log is already on the console.
- **The console's own text** (ESP_LOG, printf) is written by the task that logs it, at the line's speed: the driver has no TX buffer. With one, text left faster than the PCB's USB-Serial-JTAG secondary output carries it and lines were lost there (the console link and the tests read that port).
- **Initialization** (`sys_uart_init`): installs the UART driver (RX buffer only; `stdout` moves to the driver) and the USB-Serial-JTAG driver, registers the link and the provider, and starts the TX and RX tasks. Call after `router_init()` and `sys_connector_init()`.
- Errors on the RX task end in `err_report` (log + errors stream).

Wire format of the frames: ROUTER.MD.

## Errors (`ERR_MOD_SYS_UART` = 13)

| Error | Severity | Context / Fields | Description |
|---|---|---|---|
| `ERR_UART_LINE_BAD` | LOW | `u16 len` | `#R:` line with odd digits, non-hex characters, or exceeding maximum line length |
| `ERR_UART_BAD_ENDPOINT` | MEDIUM | `u32 endpoint` | Connector binding to an endpoint other than `SYS_UART_ENDPOINT` (0) |
| `ERR_UART_TX_FULL` | LOW | `u16 len` | The TX queue stayed full for 1 s: the frame (an answer or a stream frame) is dropped |

## Limits

- Largest frame is bounded by `CONFIG_ROUTER_FRAME_MAX` (router link) and `CONFIG_SYS_CONNECTOR_FRAME_MAX` (connector provider).
- Single endpoint `SYS_UART_ENDPOINT` (0).
- Incoming lines without the `#R:` prefix are silently ignored as console noise.

## Tests

Host test scripts (`router_tests.py` in `.claude/skills/runit-esp/scripts/`) use `TRANSPORT_UART` over serial/USB-Serial-JTAG as the primary RPC link and stream receiver for connector bindings.
