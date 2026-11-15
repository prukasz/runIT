# sys_stream

Read `SYS_STREAM.MD` in this folder before changing anything here.

- Wire format, do not renumber or re-layout: `MODULE_ID_STREAM` (`0x29`), `SYS_STREAM_TELEMETRY`..`_EVENTS`, the id ranges, `SYS_STREAM_FLAG_*`, `sys_stream_target_e`, the ops in `STREAM_OPERATIONS` (`router/sys_stream_router.h`), `sys_stream_cfg_t` (2 B), `sys_stream_ref_t` (1 B), `sys_stream_source_t` (2 B), `sys_stream_sink_t` (3 B), `sys_stream_head_t` (3 B), `sys_stream_target_t` (3 B), and `ERR_SYS_STREAM_MAP`. `contract_stream.json` and `errors/errors_sys_stream.json` follow every change.
- Streams have no `dir` field and no per-stream `seq`: whether a stream rejects `sys_stream_send` is decided solely by having a `SYS_STREAM_TARGET_SOURCE` target, and PUSH frame counters belong to `sys_session` per `(peer, class)`.
- Every send out of the stream task is `NOWAIT`; what a target does not take is freed, never reported (errors on `SYS_STREAM_ERRORS` would loop) and not counted: no per-target statistics.
- A system stream is its id (below `SYS_STREAM_ID_USER_FIRST`): no state field, no marking call.
- Public API is the board's code's (init, create, listen, unlisten, sink, send, is task); what only the router does is in `internal/`.
- No lock inversion: `sys_data_provider_open()` / `_close()`, a send and a callback never run under `sys_stream_mutex`.
- Keep `SYS_STREAM.MD` and `host/` tests current with every change.
