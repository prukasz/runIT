# sys_connector

Spec: `SYS_CONNECTOR.MD` (this folder).

- Every change in this folder is followed by an edit of this file, if it is needed.
- Transport IDs (`transport_e`) and the system connectors (`SYS_CONNECTOR_*`) live in `sys_connector_config.h` and are wire format: never renumber them. A connector is one byte, the first byte of its frames: the API, the commands and the errors call it `connector`, a binding is `provider` + `endpoint`. Error tags are wire format too (code 1 is unused: never assign it).
- Changing `transport_e`, the system connectors, the error map or `contract_connector.json` requires an update in `templates/`: run `sh templates/sync.sh` (regenerates `enums.json` / `errors.json`, refreshes `runit-app/generated/`, validates). Commit with `--check` green.
- Commands and their answers never go through connectors. They use router links (ROUTER.MD).
- `sys_connector_send` never blocks and never calls a provider. A link's `send` with `answer` false runs only on the connector task.
- Providers are the router's links (`router_link_get`): the connector keeps no transport registry of its own.
- `sys_connector.h` is what other components use; what only the component's router uses is in `internal/sys_connector_internal.h`, off the public include path.
- Provider `max_frame` runs inside `sys_connector_max_payload`, so any task may call that, except one holding a transport's lock.
- Errors in the connector task are logged (`err_log`), never reported: a report would queue a record back into this task and loop.
- A frame that a binding can't carry is dropped there (`ERR_CONN_TOO_LONG`), never cut.
- The system connectors (logs, errors, telemetry, events) can't be removed. Keep at least one binding on them.
- One byte, one connector: `create` refuses a byte that has one (the system ones exist from init) and `sys_connector_create` the router's answer byte (`CONFIG_ROUTER_RESPONSE_STREAM`).
