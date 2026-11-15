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
2. `provider_uart_create` reads `OP_INFO` on `cfg->device` (`CONTRACT_UART`), sets `p->send = provider_uart_send`, sets `SYS_DATA_PROVIDER_STATE_WIRED | _UP` (a port is up while it exists) and, on a `FRAMED` port, `_FRAMED`, and binds `OP_RX_HOOK` with `.cb = on_uart_rx`, `.ctx = p`.
3. `on_uart_rx` forwards each message via `sys_data_provider_deliver()`, and the port's removal (`data == NULL`) as down via `sys_data_provider_set_up()`.
4. `provider_uart_send` delegates to `OP_SEND_FRAME` (`wait_ms = min(CONFIG_PROVIDER_UART_SEND_WAIT_MS, SYS_UART_WAIT_MAX_MS)`, `CMD_SEND_*` flags as they are) and turns `ERR_UART_BUSY` into `ERR_PROV_BUSY`.
5. `CONTRACT_PROVIDER_OP_REMOVE` (neither system nor in use) runs `provider_uart_remove`, which lets go of the hook.
