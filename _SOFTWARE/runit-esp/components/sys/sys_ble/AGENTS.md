# sys_ble

Spec: `SYS_BLE.MD` (this folder).

- Every change in this folder is followed by an edit of this file, if it is needed.
- The runIT UUIDs (0xFFE0 to 0xFFE4), the router ops (1 SVC_CREATE to 7 LIST), the layouts of their packed structs, the framing header bits (LAST 0x80, FIRST 0x40, counter mask 0x3F), the providers' ids (`SYS_BLE_PROVIDER_*`) and the error tags are wire format, published as the enum groups `ble_runit_uuid` and `ble_frag` (markers in `sys_ble.h`; `sh templates/sync.sh` after a change). The app and the descriptor depend on them: do not renumber or re-layout them. New ones go at the end.
- Changing a router op, a struct, an error or a descriptor requires `sh templates/sync.sh` (regenerates `errors.json`, refreshes `runit-app/generated/`, validates). Commit with `--check` green. `contract_ble.json` and the LIST description in SYS_BLE.MD follow the code: update them with it.
- The runIT service is created and locked in `sys_ble_link.c` only. Nothing else adds a characteristic to it or removes it.
- The provider of a run-time characteristic is made and removed by the `ble` router's ops through `sys_ble_link_char_add` / `_char_remove` / `_service_remove`, before the characteristic or service goes: a provider that is open keeps its characteristic. Its slot is static (`CONFIG_SYS_BLE_USER_PROVIDERS`), freed by the provider's destroy.
- A framed characteristic's TX item and the BLE task's scratch buffer are `CMD_FRAME_MAX` (`cmd.h`): a whole frame, the one frame size of the firmware.
- The registry lock (`BLE_LOCK`) is released around NimBLE calls that can wait on the host (add / delete a service, notify). GATT callbacks take the lock while the host is locked: keep that order.
- RX handlers, connection listeners and the providers' deliver (the session's rx, which submits to the router) run on the host task. They must not block.
- Errors raised on the host or BLE task go through `sys_ble_report`. They have no caller to return to.
- The BLE task sends the TX items and applies the staged changes. The host task runs the GAP and GATT callbacks, including the RX. Keep that split.
- The module publishes no events. A change that adds one goes through `sys_event` and updates SYS_BLE.MD and SYS_EVENT.MD together.
