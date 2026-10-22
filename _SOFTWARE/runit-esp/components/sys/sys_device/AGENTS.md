# sys_device

Spec: `SYS_DEVICE.MD` (this folder). Writing a driver: `SYS_DEVICE_DRIVER.MD`.

- Every change in this folder is followed by an edit of this file, if it is needed.
- Op numbers, contract IDs (`SYS_DEVICE_CONTRACT_*`), device type IDs (`registry/sys_device_types.h`) and error tags are wire format or stored IDs: do not renumber them. Add new ones at the end.
- Changing `SYS_DEVICE_OPERATIONS`, a contract ID, the error map (`errors/`) or `registry/contract_device.json` requires an update in `templates/`: run `sh templates/sync.sh` (regenerates `enums.json` / `errors.json`, refreshes `runit-app/generated/`, validates). Commit with `--check` green.
- SUSPEND and RESUME are a type's `suspend` / `resume` hooks; REMOVE, INFO, OPS and ERROR_POLICY are the registry's. A type's op table lists none of them (asserted at boot).
- The registry owns the `suspended` flag: a hook never sets it.
- The sweep frames (`[05][01][FF FF][op]`, `SYS_DEVICE_OP_*`) must match the frames the onboard actions store in `components/app/runit/runit_onboard_actions.c`. Change them together.
- A driver holds no lock of its own across `sys_device_call` or `sys_event_publish`, and an op waits only for its own I/O (SYS_DEVICE.MD "Concurrency").
- Lock order: a contract guard's own lock, then the registry lock. A guard is never reached from inside a device op.
- The error policy is set at boot (`sys_device_set_*`). Only the app's error sink (`runit_error_policy.c`) calls `sys_device_handle_error`.
