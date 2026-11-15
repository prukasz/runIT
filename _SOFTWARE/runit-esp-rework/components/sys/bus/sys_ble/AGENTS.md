# sys_ble

Spec: `SYS_BLE.MD` (this folder).

- Every change in this folder is followed by an edit of `SYS_BLE.MD`, if it is needed.
- Wire format: the runIT UUIDs (0xFFE0 to 0xFFE2), the fragment header bits (LAST 0x80, FIRST 0x40, counter 0x3F), `MODULE_ID_BLE` (0x2E), the op numbers (`BLE_OPERATIONS`), the packed frames, the service and characteristic flags and the error tags. `contract_ble.json`, the LIST description in SYS_BLE.MD and the enum markers in `sys_ble.h` follow every change.
- Layers: the module knows no provider, session or stream. `provider_ble` is its one client of `sys_ble_hook` / `sys_ble_send`; the table's calls (`internal/`) are the router's only.
- The runIT service is created and locked by `sys_ble_init` only. Nothing adds to it or removes it.
- The registry lock (`g_ble_lock`) is released around NimBLE calls that wait on the host (add / delete a service, notify): the GATT callbacks take it while the host is locked. Hooks are called outside it.
- The BLE task sends the TX items and applies the staged changes (the one place that syncs); the host task runs the GAP and GATT callbacks, the RX and the framing's joining. Keep that split. Hooks run on the host task and must not block.
- Errors on the host or BLE task go to `err_report`: they have no caller.
- No central role, no pairing, no MTU request from the board (SYS_BLE.MD "Connection"): adding one is a change of `sdkconfig.defaults` and SYS_BLE.MD together.
