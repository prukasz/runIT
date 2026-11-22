# provider_ble — BLE characteristics as a provider

Component `runit-esp/components/providers/provider_ble/`. The provider driver `SYS_DATA_PROVIDER_TYPE_BLE` (`2`): one characteristic of `sys_ble` (SYS_BLE.MD), both ways, as a provider of `sys_data_provider` (SYS_DATA_PROVIDER.MD). Built with `runit_provider()` (`WHOLE_ARCHIVE`); its one public header, `provider_ble.h`, is the CREATE cfg and its flag. The characteristic is always in and out (written by the client, notified by the board): a side that is not wanted is a link the data matrix does not have, not a setting of the provider.

## Files

| File | Role |
|---|---|
| `provider_ble.h` | The CREATE cfg `provider_ble_cfg_t` (22 B) and `PROVIDER_BLE_FRAMED` (published: `provider_ble_flag`) |
| `provider_ble.json` | The provider type's descriptor (`templates/provider.schema.json`): its type, the cfg's fields; the app builds a way's PROVIDER CREATE from it |
| `provider_ble.c` | The driver: its struct, create, remove, the port's input, the hook; `SYS_DATA_PROVIDER_TYPE_DEFINE` |
| `Kconfig` | `CONFIG_PROVIDER_BLE_SEND_WAIT_MS` (default `100`), `CONFIG_PROVIDER_BLE_TX_ITEMS` (default `4`) |

## The driver

- **CREATE cfg** (22 B): `{u16 svc, u16 chr, u8 flags, u8 rx_flags, char name[16]}`: the service; the characteristic (notify out, with a TX buffer of `CONFIG_PROVIDER_BLE_TX_ITEMS` frames, and written by the client in); `flags` bit 0 `PROVIDER_BLE_FRAMED` (the characteristic carries frames: a session's way; else raw bytes, a stream's); `rx_flags` the `CMD_SEND_*` of each write as it goes out of the port (`CMD_SEND_NOWAIT`: the takers never wait for room, the BLE host never waits; without it a taker may make the host wait, and the link may drop); `name` (NUL-ended, "" = none) the user description of the characteristic (descriptor 0x2901). The board's own are made by `app/runit` (RUNIT.MD) as system providers, `NOWAIT`.
- **Struct** `provider_ble_t`: the base `sys_data_provider_t`, then the service, the characteristic, whether it holds its hook, `rx_flags`, and whether it made the service.
- **create:** makes the service when it is new (an existing one is joined, unless it is locked: whoever makes the board's own service locks it after the characteristics, `app/runit` does), the characteristic (a UUID in use: `ERR_BLE_UUID_TAKEN`; a characteristic belongs to one provider) with `WRITE` and `NOTIFY`, and takes its hook (`sys_ble_hook`, `ctx` the provider), then applies the table. A failure takes out everything it made. `STATE_FRAMED` with the flag; not `WIRED`.
- **The hook** (on the BLE host task): a write is one message (a whole frame on a framed characteristic) out of the provider's port (`data_matrix_send`, `rx_flags`); the link's state sets the provider up or down (`sys_data_provider_set_up`): up while a client is connected, down when it leaves or the characteristic is removed.
- **The port's input:** `sys_ble_send(chr, packet, flags, wait)`, `wait` 0 with `CMD_SEND_NOWAIT`, else `CONFIG_PROVIDER_BLE_SEND_WAIT_MS`. What the characteristic does not take (`ERR_BLE_TX_FULL`, no client) is lost.
- **remove:** lets go of the hook (it waits for the hook's call in flight: when remove returns, the hook is never called again), removes the characteristic and the service it made, and applies the table.
- No own ops, no lock of its own: the registry makes the calls into it one at a time, and what comes in shares nothing with what goes out but what create set.
