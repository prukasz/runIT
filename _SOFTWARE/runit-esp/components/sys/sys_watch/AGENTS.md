# sys_watch

Spec: `SYS_WATCH.MD` (this folder).

- Every change in this folder is followed by an edit of this file, if it is needed.
- The head (`sys_watch_head_t`), `sys_watch_info_t`, the triggers (`sys_watch_trigger_e`), the target byte (`SYS_WATCH_TO_*`), the entry layout `[owner][id][status][data]`, the router ops and the error tags (`ERR_MOD_SYS_WATCH` = 19) are wire format: never renumber or re-layout them. A change runs `sh templates/sync.sh` (enums, errors, `contract_watch.json`).
- A watch has no task: its read is `router_inject`, its entry is made in `done` on the router's work task. Keep it so: an owner's entries must leave on that task, right after the read, for the order SYS_WATCH.MD "Order" promises.
- The table's mutex guards a copy and a flag, nothing more: never call the router, the session, a connector or `sys_event` under it. A read's answer carries the slot and a generation, and is dropped when the slot was taken again.
- The entry is made in the answer's `CMD_HEAD` bytes: nothing may copy the answer before `emit`.
- A listener of `sys_event` runs on the event task: it marks the watch and returns; the read is a command, never a call.
