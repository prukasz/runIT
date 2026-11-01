# sys_data_connector

Spec: `SYS_DATA_CONNECTOR.MD` (this folder).

- Every change in this folder is followed by an edit of this file, if it is needed.
- The system connectors (`SYS_DATA_CONNECTOR_*`), the binding and connector flags and states live in `sys_data_connector_config.h` and are wire format: never renumber them. 0x05 is kept for the watches' entries. Error tags are wire format too (codes 1, 5, 10, 12 and 13 are unused: never assign them).
- Changing the config groups, the error map or `contract_connector.json` requires an update in `templates/`: run `sh templates/sync.sh` (regenerates `enums.json` / `errors.json`, refreshes `runit-app/generated/`, validates). Commit with `--check` green. The app's `src/app/settings/streams.ts` reads LIST's layout and builds BIND / UNBIND: keep it in step.
- Commands and their answers never go through connectors: they are the session's (SYS_SESSION.MD).
- `sys_data_connector_send` never blocks and never calls a provider or the session. Only the connector task sends, and always `NOWAIT`: one slow target never holds the others.
- A binding is its provider's one user (`sys_data_provider_open`); the connector opens and closes it. A subscriber is a peer id, taken from the command's `src` (`router_origin()`), never from the payload. Gone peers (`sys_session_on_gone`) lose their subscriptions.
- `sys_data_connector.h` is what other components use (create and remove too: sys_remote makes its mirrors' connectors); what only the component's router uses is in `internal/sys_data_connector_internal.h`, off the public include path.
- A mirror's frame is queued with the connector's own, first byte 0 (no connector has it); its listener runs on the connector task and stays short. A mirror is a user connector: never a system one.
- No call into a provider or the session under the connector's lock: copy what is needed, release, send, then write the results back by provider and by peer.
- Errors in the connector task are logged (`err_log`), never reported: a report would queue a record back into this task and loop.
- A frame that a binding can't carry is dropped there (`ERR_PROV_TOO_LONG`), never cut.
- The system connectors (logs, errors, telemetry, events) can't be removed.
