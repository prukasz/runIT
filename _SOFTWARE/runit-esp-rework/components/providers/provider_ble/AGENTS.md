# provider_ble

Spec: `PROVIDER_BLE.MD` (this folder).

- Every change in this folder is followed by an edit of `PROVIDER_BLE.MD`, if it is needed.
- The CREATE cfg (`provider_ble_cfg_t {u16 tx, u16 rx}`, packed) and the type number (`SYS_DATA_PROVIDER_TYPE_BLE = 2`) are wire format.
- The driver uses only `sys_ble.h` (`sys_ble_hook`, `sys_ble_send`): no BLE internals, no other provider, no public header (`runit_provider()`).
- A failed create lets go of every hook it took: the registry does not call remove then.
