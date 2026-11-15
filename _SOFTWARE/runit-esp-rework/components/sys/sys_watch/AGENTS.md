# AGENTS — sys_watch

- Wire formats are fixed: `MODULE_ID_WATCH` (`0x2B`), the entry `[SYS_WATCH_CHANNEL][id][status][data]` (the channel is the module's number with bit 7 set, `SYS_SESSION_CHANNEL(MODULE_ID_WATCH)`; no `owner` byte), `sys_watch_head_t` (14 B, with the 8 B `sys_event_key_t`), `sys_watch_info_t` (16 B), `WATCH_OPERATIONS`, and `ERR_SYS_WATCH_MAP`. Keep `contract_watch.json` and `errors_sys_watch.json` matching.
- Three headers: `sys_watch.h` (wire format, init, the cached reads), `router/sys_watch_router.h`, `errors/sys_watch_errors.h`; `internal/sys_watch_internal.h` is the sources' own. A watch is added, removed and listed by commands: `sys_watch_add` / `_remove` / `_list` are the router's, not public.
- `sys_watch_mutex` (the watches) may be held over `sys_session_push` and `router_inject`: neither blocks nor calls back here. Never over `sys_event_*` (subscribe before taking it, unsubscribe after giving it) or `err_report`. `sys_watch_client_mutex` (the cached reads) is for copies only: never held over a send.
- Both timers run from init, with no start and stop by a count. One way to start a read: the tick. `ON_EVENT` only marks `pending` (`SYS_EVENT_RUN_QUEUED`, found by `(owner, id)` from the subscription's context).
- A callback from before a slot was taken again is dropped by the slot's `gen` (a read's answer, ADD's answer): the only use of `gen`.
- B3: `on_push` takes an entry only when `s->state == SLOT_ACTIVE && s->board == src`. Whatever makes what a slot holds wrong (a write, the board gone) puts it back to `ADDING` and sends ADD again: no other state, no second way to refresh.
- Keep it small: no state that only guards against a caller's mistake, no retry bookkeeping beyond `add_ms`.
