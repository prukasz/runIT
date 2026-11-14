# AGENTS — sys_watch

- Wire formats are fixed: `MODULE_ID_WATCH` (`0x2B`), `[0x80][id][status][data]` PUSH entry layout (no `owner` byte, no negative-index buffer writes), `sys_watch_head_t` (14 B, using 8 B `sys_event_key_t`), `sys_watch_info_t` (16 B), `WATCH_OPERATIONS`, and `ERR_SYS_WATCH_MAP`. Keep `contract_watch.json` and `errors_sys_watch.json` matching.
- Never call `router_inject`, `sys_session_*`, `sys_event_*`, `board_timer_*`, or `err_report` while holding `sys_watch`'s mutex or `sys_watch_client`'s mutex.
- Keep `board_timer` callbacks fast and non-blocking; run `ON_EVENT` subscriptions with `SYS_EVENT_RUN_QUEUED` so event delivery never takes locks inline inside device/publisher contexts.
- Client PUSH gate (B3): `cl_on_push` MUST only accept entries when `s->state == SLOT_ACTIVE && s->board == src`. Never accept stale entries while `SLOT_ADDING` or `SLOT_OLD`.
- Client cache slots increment `gen` on every state reset (`FREE`, `on_gone`, `sys_watch_stale`), so late `on_add_done` / `on_refresh` callbacks from prior epochs are dropped cleanly.
