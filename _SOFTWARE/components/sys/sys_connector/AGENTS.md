# sys_connector

Spec: `SYS_CONNECTOR.MD` (this folder).

- Every change in this folder is followed by an edit of this file, if it is needed.
- Do not add references to `legacy docs` unless the user asks for them.
- Transport IDs (`transport_e`), connector IDs (`sys_connector_id_e`) and stream bytes (`SYS_CONNECTOR_STREAM_*`) are wire format: never renumber them. Error tags are wire format too.
- Changing `transport_e`, `sys_connector_id_e`, the stream bytes, the error map or `contract_connector.json` requires an update in `templates/`: run `sh templates/sync.sh` (regenerates `enums.json` / `errors.json`, refreshes `generated/`, validates). Commit with `--check` green.
- Commands and their answers never go through connectors. They use router links (ROUTER.MD).
- `sys_connector_send` never blocks and never calls a provider. Provider `send` runs only on the connector task.
- Provider `max_frame` runs inside `sys_connector_max_payload`, so any task may call that, except one holding a transport's lock.
- Errors in the connector task are logged (`err_log`), never reported: a report would queue a record back into this task and loop.
- A frame that a binding can't carry is dropped there (`ERR_CONN_TOO_LONG`), never cut.
- The system connectors (logs, errors, telemetry, events) can't be removed. Keep at least one binding on them.
- A user connector never takes a system stream byte or the router's answer byte: `sys_connector_create` refuses them (`stream_reserved`). Keep that list in step with the stream bytes in `sys_connector.h`.
