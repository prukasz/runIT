# provider_ble

Spec: `PROVIDER_BLE.MD` (this folder).

- Every change in this folder is followed by an edit of `PROVIDER_BLE.MD`, if it is needed.
- The CREATE cfg (`provider_ble_cfg_t {u16 svc, u16 chr, u8 flags, u8 rx_flags, char name[16]}`, packed, 22 B, in `provider_ble.h`, which `app/runit` includes) and the type number (`SYS_DATA_PROVIDER_TYPE_BLE = 2`) are wire format: a change of the cfg changes `provider_ble.json` in the same change (`templates/sync.sh`).
- The driver uses only `sys_ble.h` (the table's calls, `sys_ble_hook`, `sys_ble_send`): no BLE internals, no other provider, and no public header but `provider_ble.h` (the CREATE cfg and its flag, nothing else). The service's lock is its maker's (`app/runit`), not the driver's.
- A failed create takes out everything it made (hooks, characteristics, its service): the registry does not call remove then.
