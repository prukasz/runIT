# sys_device

Spec: `SYS_DEVICE.MD` (this folder).

- Every change in this folder is followed by an edit of `SYS_DEVICE.MD` and of this file, if it is needed.
- Three headers, three readers: `sys_device.h` (every caller), `sys_device_driver.h` (a driver), `sys_device_hooks.h` (the app's wiring). `slots/`, `registry/`, `call/` and `remote/` are private: no other component includes them.
- The registry names no driver, no contract but its own (`0x20`) and no module above it. A driver registers itself (`SYS_DEVICE_TYPE_DEFINE`), a contract claims its own ID (`SYS_DEVICE_CONTRACT_DEFINE`), the app wires itself in through the hooks. Never add a list of types or contracts.
- Wire format, never renumbered: the registry's op numbers (0x07 stays unused), `SYS_DEVICE_CREATE_*`, `sys_device_info_t`, `sys_device_remote_t`, the type IDs, the contract IDs, the events and their payloads, the error tags.
- A contract ID is spelled as two hex digits, the same in `SYS_DEVICE_CONTRACT_DECLARE` and `SYS_DEVICE_CONTRACT_DEFINE`; the define is in exactly one source of the contract's component, and a source that only holds it is linked whole.
- One lock per device, not recursive; the only way past it is `SYS_DEVICE_OP_NOLOCK`. Never add a recursive lock or a lock of the whole registry.
- The lock of a slot is always taken with the timeout (`sys_device_slot_lock`); nothing waits for it forever. A step (`sys_device_step`) takes it only when it is free (`sys_device_slot_try_lock`) and is skipped otherwise: never make the timers' task wait for a device.
- A step runs only on the device of its generation, not suspended and not refused by the safe-state sweep; a skipped step is no error. The generation is taken at the start of CREATE (`sys_device_slot_begin_create`), before `create()` hands it out.
- Lock order: a device's lock, then the locks of the devices it uses, then the table's critical section (`s_table_lock`). Nothing waits, allocates, builds an error or calls out inside the critical section.
- Every write to a slot's flags is made with the slot's lock held and inside the table's critical section. The fields of a slot from `type` on are the device's and cleared by `sys_device_slot_uninstall`; a field that must outlive the device (the lock, `gen`) goes before `type`.
- REMOVE marks the device as being removed in the critical section `sys_device_slot_use` runs in. Never split the check of `used_by` from the mark.
- A use never closes a cycle (`ERR_DEV_CYCLE`): the sweep's order and the lock order rest on it.
- Nothing is delivered to a listener while the task holds the lock of a slot: the task's outermost call holds its events and releases them after the last lock. `publish_change` runs inside the call, after the unlock.
- Per-task state (`s_depth`, `s_running`, `s_caller`, `s_sweeping`) is restored on every way out of a call; the room for held events is on the outermost call's frame, never in TLS.
- A device's resources are released by the registry, the last added first, on REMOVE and on a failed `create()`. A type has no uninstall hook; a failed `create()` never runs `suspend()`.
- CREATED and REMOVED carry the slot's generation; the generation is read under the lock.
- While a SUSPEND sweep runs, a write from no device and a CREATE are refused; a device's own calls pass. Keep both halves: without the second a `suspend()` hook can't reach the devices it uses.
- A driver's source has one shape: `SYS_DEVICE_TYPE_DECLARE` (the type ID, written once), the struct and `#define SYS_DEVICE_CTX`, the handlers, the typed hooks, the op table, `SYS_DEVICE_TYPE_DEFINE` last. A driver never fills `sys_device_type_t`, casts `ctx` or writes its type ID a second time.
- Every op is a row of a table, a contract's or the device's own (`SYS_DEVICE_OWN_DECLARE`): its number, key, value and flags come from there. Never a `#define` of an own op, a hand-written row with `.flags`, or a handler named by hand.
- An op's handler follows its table row: `OP_DECLARE_WRITE_TAIL` for an op marked `SYS_DEVICE_OP_TAIL`, `OP_DECLARE_WRITE` for every other (both are asserted). Never write a `cmd_f` by hand to parse a payload.
- The interceptor's stages run without any lock of the registry; `done` is called once for every call `fn` let through, whatever happened to it, and never for one `fn` refused.
- What a device gives out to one device has an owner, `sys_device_caller()` of the op that took it, named in the error and checked by the ops that use and free it. A driver never checks the type of another device.
- The error map and `errors/errors_sys_device.json` change together; a tag no code raises is removed from both. After a change run `sh templates/sync.sh`.
- Host tests: `host/` (SYS_DEVICE.MD "Tests"). A change here is run with `python test_host/run.py sys_device` from `runit-esp/`. Every test leaves no device behind (`tearDown` sweeps REMOVE); a board device can't be removed, so the test that makes one runs last. What a test leaves waiting (a blocked op, a `suspend()` that waits) is let go before `tearDown`'s sweep reaches it.
