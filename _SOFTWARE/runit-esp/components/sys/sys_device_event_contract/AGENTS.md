# sys_device_event_contract

Spec: `SYS_DEVICE_EVENT_CONTRACT.MD` (this folder).

- Every change in this folder updates this file if needed.
- Decoupling invariant: keeps the `EVENT` contract (`MODULE_ID_EVENT`) and subscription-to-device glue out of both `sys_event` and `sys_device`. Do not add `sys_device` dependencies to `sys_event` or `sys_event` subscription APIs to `sys_device`.
- The contract `MODULE_ID_EVENT` (`EVENT`), `CONTRACT_EVENT_OP_ON_EVENT` (`0x00`), and the `OP_DECLARE_EVENT` / `sys_device_event_subscribe_op` contract stay aligned across `sys_device_event_contract.h`, `sys_device_event_contract.c`, and `contract_event.json` (`sh templates/sync.sh`).
- `sys_device_event_subscribe_op` accepts only `SYS_DEVICE_CONTRACT_OWN` ops and `CONTRACT_EVENT_OP_ON_EVENT`; delivery uses `sys_device_deliver` with packed `[op u16][gen u8][owner u8]`, never a raw pointer to driver state or slot.
- `OP_ON_EVENT` carries `SYS_DEVICE_OP_ALWAYS | SYS_DEVICE_OP_TAIL`: handlers decide whether to act when `sys_device_self_suspended()` is true.
