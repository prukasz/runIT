# sys_stream

Spec: `SYS_STREAM.MD` (this folder).

- Every change in this folder is followed by an edit of this file, if it is needed.
- The system streams (`SYS_STREAM_*`), the id ranges, the direction, the flags, the target kinds and the state live in `sys_stream_config.h` and are wire format: never renumber them. A stream id is below `SYS_SESSION_CHANNEL_FIRST` (0x80): from there the byte is the session's. Error tags are wire format too (codes 1, 5, 10, 12, 13 and 14 are unused: never assign them), and so are the router's op codes (0x05, 0x06, 0x08 and 0x0B are unused).
- Changing the config groups, the error map or `contract_stream.json` requires an update in `templates/`: run `sh templates/sync.sh` (regenerates `enums.json` / `errors.json`, refreshes `runit-app/generated/`, validates). The app's stream settings read LIST's layout and build BIND / UNBIND: keep them in step.
- Commands and their answers never go through streams: they are the session's (SYS_SESSION.MD).
- A stream has one direction and one list of targets; it keeps nothing. Storage is the consumer's: no buffer, read or count of what came in is added here.
- `sys_stream_send` never blocks and never calls a provider or the session. Only the stream task sends, and always `NOWAIT`: one slow target never holds the others. What comes in on an IN binding is queued on the driver's RX task: a copy, nothing else.
- An IN stream's binding holds its provider's rx hook (`sys_data_provider_open`, close); an OUT stream's holds nothing, it only names the provider. A subscriber is a peer id, taken from the command's `src` (`router_origin()`), never from the payload. Gone peers (`sys_session_on_gone`) lose their subscriptions.
- `sys_stream.h` is what other components use; what only the component's router uses (LIST's copy of the streams) is in `internal/sys_stream_internal.h`, off the public include path. The router's ops and wire layouts are in `router/sys_stream_router.h`, each op's define directly above the frame it uses; `sys_stream.h` only forward-declares `sys_stream_cfg_t`. Every `struct` name in `contract_stream.json` is a C type of the router header.
- No call into a provider, the session or a callback under the stream's lock: copy the targets, release, deliver, then write the results back by target.
- Errors in the stream task are logged (`err_log`), never reported: a report would queue a record back into this task and loop.
- A message that a binding can't carry is dropped there (`ERR_PROV_TOO_LONG`), never cut.
- This component makes no stream: the board's own (logs, errors, telemetry, events) are created by the app layer (`runit_board_cfg.c`) and marked with `sys_stream_mark_system`; then they can't be removed.
