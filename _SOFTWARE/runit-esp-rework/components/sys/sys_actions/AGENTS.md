# sys_actions

Spec: `SYS_ACTIONS.MD` (this folder).

- Every change in this folder is followed by an edit of `SYS_ACTIONS.MD`, if it is needed.
- `MODULE_ID_ACTIONS` (0x2C), the IDs and classes, the op codes (`ACTIONS_OPERATIONS` in `router/sys_actions_router.h`), the frames, the error tags and the NVS key format `a<8 hex digits>` are wire format or stored data: never renumber them. `contract_actions.json` and `errors/errors_sys_actions.json` follow every change.
- The size limit of a record is `CONFIG_SYS_ACTIONS_MAX_BYTES`, of every class, never a literal.
- `s_draft` is touched only under `sys_actions_mutex`. A replay never holds it.
- A record runs only on a router task (`router_origin()`); anything else queues a RUN (`sys_actions_request`).
- The onboard actions (`k_onboard` in `sys_actions.c`) are written by `sys_actions_init` at every boot. Their IDs are fixed (`SYS_ACTIONS_ONBOARD_*`: the app and stored error policies name them); their frames follow the device router's frame (`sys_device_router.h`), the sweep ops (`CONTRACT_SYS_DEVICE_OP_*`) and sys RESTART (`SYS_OP_RESTART`, `sys_system_router.h`).
- Layers: the store knows the router, the registry's op codes and sys's RESTART code, nothing above; the modules that keep records (project, board profile) build on it, never the other way.
