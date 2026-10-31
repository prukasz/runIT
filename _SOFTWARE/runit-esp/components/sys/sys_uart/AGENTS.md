# sys_uart

Spec: `SYS_UART.MD` (this folder).

- Every change in this folder is followed by an edit of this file, if it is needed.
- Transport ID `TRANSPORT_UART` (1) and endpoint `SYS_UART_ENDPOINT` (0) are wire format: do not renumber or change them.
- Framing format: lines strictly start with `#R:` and end with `\n`. TX outputs uppercase hex; RX accepts upper or lower case hex.
- Static buffers: line assemblers (`s_uart_rx`, `s_usb_rx`) and frame buffer (`s_frame`) are static, sized for `CMD_FRAME_MAX` (`cmd.h`).
- Error tags in `ERR_SYS_UART_MAP` (`ERR_UART_LINE_BAD`, `ERR_UART_BAD_ENDPOINT`, `ERR_UART_TX_FULL`) are wire format (`ERR_MOD_SYS_UART` = 13). Changing them requires `sh templates/sync.sh` (regenerates `errors.json`, refreshes `runit-app/generated/`, validates). Commit with `--check` green.
- TX: `link_send` only queues (`sys_uart_tx_rb`); `write_line` runs on the TX task alone, under the `stdout` file lock (`flockfile` / `funlockfile`) so concurrent `ESP_LOG` output cannot interleave inside a `#R:` line. Nothing writes a frame to the console inline.
- RX processing: lines without the `#R:` prefix are ignored (console log noise, terminal input). Corrupted lines (odd digits, non-hex, overflow) report `ERR_UART_LINE_BAD`. Valid frames go to the router via `router_link_receive(TRANSPORT_UART, s_frame, size)`.
- Connector provider: `k_provider` sends outbound stream frames (errors, telemetry) on `SYS_UART_ENDPOINT` (0). Any other endpoint is rejected with `ERR_UART_BAD_ENDPOINT`.
- Initialization: `sys_uart_init` installs the console drivers, redirects `stdout` VFS to the UART driver, registers `TRANSPORT_UART` as a router link (the connectors' provider too), and launches the TX and RX tasks. Call once after `router_init()` and `sys_data_connector_init()`.

