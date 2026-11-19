# provider_uart — a UART device as a provider

Component `runit-esp-rework/components/providers/provider_uart/`. The provider driver `SYS_DATA_PROVIDER_TYPE_UART` (`1`): any `CONTRACT_UART` device (`sys_uart_contract`) as a provider of `sys_data_provider` (SYS_DATA_PROVIDER.MD). Built with `runit_provider()` (`WHOLE_ARCHIVE`, no public header).

## Files

| File | Role |
|---|---|
| `provider_uart.c` | The driver: its cfg, its struct, create, remove, the port's input, the UART's reader; `SYS_DATA_PROVIDER_TYPE_DEFINE` |
| `CMakeLists.txt` | `runit_provider()`, `PRIV_REQUIRES errors sys_data_provider sys_device sys_uart_contract` |
| `Kconfig` | `CONFIG_PROVIDER_UART_SEND_WAIT_MS` (default `1000`, range `10..1000`, at most `SYS_UART_WAIT_MAX_MS`) |

## The driver

- **CREATE cfg** (1 B): `{device u8}`, a `CONTRACT_UART` device.
- **Struct** `provider_uart_t`: the base `sys_data_provider_t`, then the UART's device ID.
- **create:** reads the UART's `OP_INFO` (no such UART: the provider is not made), sets `STATE_WIRED | STATE_UP` (a UART is up as long as it exists) and, on a `FRAMED` port, `STATE_FRAMED`, then hooks its reader (`OP_RX_HOOK`, `ctx` the provider; a UART with a reader already: `ERR_UART_IN_USE`, not made).
- **The port's input:** the packet onto the UART with `OP_SEND_FRAME`, its `CMD_SEND_*` flags as they are, waiting for room up to `CONFIG_PROVIDER_UART_SEND_WAIT_MS` (`NOWAIT`: none). What the UART does not take (`ERR_UART_BUSY`) is lost.
- **The reader** (on the UART's RX task): each message out of the provider's port (`data_matrix_send`, no flags); the UART removed (`data` NULL): `sys_data_provider_set_up(p, false)`.
- **remove:** lets go of the hook (a UART removed before: nothing to let go). The UART calls its reader outside its lock, so a message in flight at REMOVE may still reach the reader: it goes out of a port no longer in the data matrix, so nowhere (SYS_DATA_PROVIDER.MD "Concurrency").
- No own ops, no lock of its own: the registry makes the calls into it one at a time, and its RX side shares nothing with its TX but the device ID, set once in create.
