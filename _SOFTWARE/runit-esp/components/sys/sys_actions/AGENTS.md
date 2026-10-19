# sys_actions

Spec: `SYS_ACTIONS.MD` (this folder).

- Every change in this folder is followed by an edit of this file, if it is needed.
- IDs, op codes, error tags and the NVS key format `a<8 hex digits>` are wire format or stored data: do not renumber them.- Op numbers live in `sys_actions_router.h` and `contract_actions.json`; keep both in step, and the answer types in the descriptor must name real C types.
- Modifying a contract (`contract_actions.json`: op, args, returns), an op number, an error tag or a `#ref-*` group requires an update in `templates/`: run `sh templates/sync.sh` (regenerates `enums.json` / `errors.json`, refreshes `runit-app/generated/`, validates). Commit with `--check` green.
- The size limit of a record comes from its class: use `sys_actions_max_bytes(id)`, never a literal.
- `s_session` and `s_image` are touched only under `sys_actions_mutex`. A replay never holds it.
- The firmware's onboard actions (`runit_onboard_actions.c`) are written at every boot. Their IDs are Kconfig options (`RUNIT_ACTION_ID_*`); the frames they store must match the sweep ops in `sys_device` and the sys RESTART (`SYS_SYSTEM_OP_RESTART`, `sys_system.h`).
