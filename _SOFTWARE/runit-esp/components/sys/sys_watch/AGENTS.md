# sys_watch

Spec: `SYS_WATCH.MD` (this folder).

- Every change in this folder is followed by an edit of this file, if it is needed.
- The head (`sys_watch_head_t`), `sys_watch_info_t`, the triggers (`sys_watch_trigger_e`), the target byte (`SYS_WATCH_TO_*`), the entry layout `[owner][id][status][data]`, the router ops and the error tags (`ERR_MOD_SYS_WATCH` = 19) are wire format: never renumber or re-layout them. A change runs `sh templates/sync.sh` (enums, errors, `contract_watch.json`).
- A watch has no task: its read is `router_inject`, its entry is made in `done` on the router's work task. Keep it so: an owner's entries must leave on that task, right after the read, for the order SYS_WATCH.MD "Order" promises.
- The table's mutex guards a copy and a flag, nothing more: never call the router, the session, a connector or `sys_event` under it. A read's answer carries the slot and a generation, and is dropped when the slot was taken again.
- The entry is made in the answer's `CMD_HEAD` bytes: nothing may copy the answer before `emit`.
- The client (`sys_watch_client.c`) has its own table and mutex: the same rule, never a call into the session or a callback under it. Its callbacks (`sys_watch_entry_f`) run on the RX task of the entry and never block; the refresh read of a stale copy and the ADD / REMOVE of a cached read are calls with an answer callback, never waited for.
- A cached read is known by its board and its request bytes, and its ids on the other board are the client's own (1..255, never the owner's): keep `(board, id)` unique in the client's table.
- A listener of `sys_event` runs on the event task: it marks the watch and returns; the read is a command, never a call.
