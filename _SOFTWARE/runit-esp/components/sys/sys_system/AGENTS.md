# sys_system

Spec: `SYS_SYSTEM.MD` (this folder).

- Every change in this folder is followed by an edit of this file, if it is needed.
- `ROUTER_KEY_SYS` (0x01), the op numbers (`OP_*` in `sys_system_router.c`, `SYS_SYSTEM_OP_*` in `sys_system.h`), the packed structs and the values of `sys_safe_reason_e` are wire format: never renumber. `SYS_SYSTEM_OP_RESTART` is also stored in the onboard hard-reset action (`runit_onboard_actions.c`).
- Changing an op, a struct, `sys_safe_reason_e` or `contract_sys.json` requires `sh templates/sync.sh` (regenerates `enums.json`, refreshes `runit-app/generated/`, validates). The app reads the reasons by the enum's name (`useVm.ts`). Commit with `--check` green.
- The dead-man stays runtime only: never stored, so a board that restarts runs its program offline.
- The dead-man is the session's: no timer here. Its gone listener never enters the safe state itself (it runs on a driver's RX task or the session's timer): it notes the reason and has the router's urgent task run the handler (`router_run_urgent`).
- Safe state means the app's handler; this module only notes the reason and calls it. What "safe" is (VM stop, devices suspended) stays in `runit`.
