# provider_ble — BLE characteristic-to-provider driver

Component `runit-esp-rework/components/providers/provider_ble/`. Self-registering provider driver (`SYS_DATA_PROVIDER_TYPE_BLE = 2`) bridging characteristics of `sys_ble` (SYS_BLE.MD) into `sys_data_provider` via `runit_provider()` (`WHOLE_ARCHIVE`, no public header).

| File | Role |
|---|---|
| `provider_ble.c` | Registers `SYS_DATA_PROVIDER_TYPE_BLE`: the hooks, the send |
| `Kconfig` | `CONFIG_PROVIDER_BLE_SEND_WAIT_MS` (default `100`) |

## Lifecycle

1. Created by `CONTRACT_PROVIDER_OP_CREATE` (C or the provider router, key `0x0D`) with `type = 2` and the 4-byte cfg `provider_ble_cfg_t {u16 tx, u16 rx}`: the characteristic frames go out on and the one they come in from, 0 = none, at least one. The board's own: `main` makes `{0xFFE1, 0xFFE2}` (the runIT pair) a system provider and attaches a client's session to it.
2. Create takes the hook of `rx` and of `tx` (`sys_ble_hook`, `ctx` = the provider; a failure lets go of what it took). The provider is `FRAMED` when both characteristics are: a session's way; else raw, a stream's.
3. A hook's write is one message to the provider's user (`sys_data_provider_deliver`; a whole frame on a framed characteristic). The link's state sets the provider up or down (`sys_data_provider_set_up`): up while a client is connected, down when it leaves or the characteristic is removed, whose hook is then let go. Not `WIRED`.
4. Send is `sys_ble_send(tx, frame, flags, wait)`: `wait` 0 with `CMD_SEND_NOWAIT`, else `CONFIG_PROVIDER_BLE_SEND_WAIT_MS`; `ERR_BLE_TX_FULL` becomes `ERR_PROV_BUSY`. A provider with no `tx` sends `ERR_BLE_NOT_FOUND`.
5. `CONTRACT_PROVIDER_OP_REMOVE` lets go of both hooks.
