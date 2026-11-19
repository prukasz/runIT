# sys_ota

Spec: `SYS_OTA.MD` (this folder).

- Every change in this folder is followed by an edit of this file, if it is needed.
- `ROUTER_KEY_OTA` (0x12), the op numbers, the packed structs, `sys_ota_state_e` and the error tags (`ERR_MOD_SYS_OTA` = 23) are wire format: never renumber. A change to them or to `contract_ota.json` runs `sh templates/sync.sh`.
- A slot's size is fixed by `partitions.csv`, and a partition table only changes by cable.
- A write never starts while the guard says busy: `runit` owns what busy means (the VM), this module knows nothing of the VM.
