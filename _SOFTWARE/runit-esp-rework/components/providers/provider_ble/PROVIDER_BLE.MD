# provider_ble — BLE characteristic-to-provider driver

Component `runit-esp-rework/components/providers/provider_ble/`. Self-registering provider driver (`SYS_DATA_PROVIDER_TYPE_BLE = 2`) bridging characteristics of `sys_ble` (SYS_BLE.MD) into `sys_data_provider` via `runit_provider()` (`WHOLE_ARCHIVE`; its one public header, `provider_ble.h`, is the CREATE cfg and its flags).

| File | Role |
|---|---|
| `provider_ble.c` | Registers `SYS_DATA_PROVIDER_TYPE_BLE`: the hooks, the port's input |
| `Kconfig` | `CONFIG_PROVIDER_BLE_SEND_WAIT_MS` (default `100`), `CONFIG_PROVIDER_BLE_TX_ITEMS` (default `4`) |

## Lifecycle

1. Created by `CONTRACT_PROVIDER_OP_CREATE` (C or the provider router, key `0x0D`) with `type = 2` and the 23-byte cfg `provider_ble_cfg_t {u16 svc, u16 tx, u16 rx, u8 flags, char name[16]}`: the service, the characteristic frames go out on (notify, with a TX buffer of `CONFIG_PROVIDER_BLE_TX_ITEMS` frames) and the one they come in from (written by the client), 0 = none, at least one of them. `flags`: bit 0 FRAMED (both characteristics carry frames: a session's way). `name` (NUL-ended, "" = none) is the user description of the characteristics (descriptor 0x2901): with both `tx` and `rx` the name is followed by ` TX` and ` RX`, with one it is the name. The board's own pair is made by `app/runit` (RUNIT.MD) as a system provider.
2. Create makes the service when it is new (an existing one is joined, unless it is locked: whoever makes the board's own service locks it after the characteristics, `app/runit` does), the characteristics, and takes their hooks (`sys_ble_hook`, `ctx` = the provider), then applies the table. A failure takes out everything it made. The provider is `FRAMED` with the flag: a session's way; else raw, a stream's. A characteristic belongs to one provider: a UUID in use is `ERR_BLE_UUID_TAKEN`.
3. A hook's write is one message to the provider's user (`sys_data_provider_deliver`; a whole frame on a framed characteristic). The link's state sets the provider up or down (`sys_data_provider_set_up`): up while a client is connected, down when it leaves or the characteristic is removed, whose hook is then let go. Not `WIRED`.
4. The port's input (`provider_ble_pass`) is `sys_ble_send(tx, frame, flags, wait)`: `wait` 0 with `CMD_SEND_NOWAIT`, else `CONFIG_PROVIDER_BLE_SEND_WAIT_MS`. What the characteristic does not take (`ERR_BLE_TX_FULL`, a provider with no `tx`) is dropped.
5. `CONTRACT_PROVIDER_OP_REMOVE` lets go of the hooks, removes the characteristics and the service it made, and applies the table.
