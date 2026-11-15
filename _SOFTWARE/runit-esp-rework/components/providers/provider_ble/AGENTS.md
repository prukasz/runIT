# provider_ble

Spec: `PROVIDER_BLE.MD` (this folder).

- Every change in this folder is followed by an edit of `PROVIDER_BLE.MD`, if it is needed.
- The CREATE cfg (`provider_ble_cfg_t {u16 svc, u16 tx, u16 rx, u8 flags, char name[16]}`, packed, 23 B; `app/runit` writes a copy of it) and the type number (`SYS_DATA_PROVIDER_TYPE_BLE = 2`) are wire format.
- The driver uses only `sys_ble.h` (the table's calls, `sys_ble_hook`, `sys_ble_send`): no BLE internals, no other provider, no public header (`runit_provider()`).
- A failed create takes out everything it made (hooks, characteristics, its service): the registry does not call remove then.
