# provider_uart — UART device-to-provider driver

Component `runit-esp-rework/components/providers/provider_uart/`. Self-registering provider driver (`SYS_DATA_PROVIDER_TYPE_UART = 1`) bridging any `CONTRACT_UART` device (`sys_uart_contract`) into `sys_data_provider` via `runit_provider()` (`WHOLE_ARCHIVE`, zero public header, zero `INCLUDE_DIRS`).

## Files

| File | Role |
|---|---|
| `provider_uart.c` | Self-registers `SYS_DATA_PROVIDER_TYPE_UART`, `OP_INFO` probe, `OP_RX_HOOK` forwarding, `OP_SEND_FRAME` TX |
| `CMakeLists.txt` | Uses `components/providers/provider.cmake` (`runit_provider()`) with `PRIV_REQUIRES errors sys_data_provider sys_device sys_uart_contract` |
| `Kconfig` | `CONFIG_PROVIDER_UART_SEND_WAIT_MS` (default `1000`, range `10..1000`) |

## Lifecycle

1. Created via `sys_data_provider_write(id, CONTRACT_PROVIDER_OP_CREATE, ...)` (or via `sys_data_provider_router` on key `0x0D`) with `type = SYS_DATA_PROVIDER_TYPE_UART` (`1`), optional `SYS_DATA_PROVIDER_CREATE_SYSTEM`, and 1-byte `cfg = { uint8_t device }`.
2. `provider_uart_create` reads `OP_INFO` on `cfg->device` (`CONTRACT_UART`), sets `p->send = provider_uart_send`, sets `SYS_DATA_PROVIDER_STATE_WIRED` and (when `mode == SYS_UART_MODE_FRAMED`) `SYS_DATA_PROVIDER_STATE_FRAMED` on `p->state`, and binds `OP_RX_HOOK` with `.cb = on_uart_rx`, `.ctx = p`, `.allow_send = !framed`.
3. `on_uart_rx` forwards link up/down (`data == NULL`) via `sys_data_provider_set_up()` and received frames via `sys_data_provider_deliver()`.
4. `provider_uart_send` reads `ctx = p->driver` and delegates to `OP_SEND_FRAME` (`wait_ms = min(CONFIG_PROVIDER_UART_SEND_WAIT_MS, SYS_UART_WAIT_MAX_MS)`), passes `CMD_SEND_*` `flags` verbatim and `.token = p`, and converts `ERR_UART_BUSY` to `ERR_PROV_BUSY`.
5. `CONTRACT_PROVIDER_OP_REMOVE` (if neither system nor in use) invokes `provider_uart_remove`, which unhooks `OP_RX_HOOK` cleanly.
