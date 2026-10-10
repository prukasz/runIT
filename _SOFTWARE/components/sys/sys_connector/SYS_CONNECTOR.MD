# sys_connector — outbound data streams (for agents)

Component `components/sys/sys_connector/`. A **connector** is a logical outbound stream with a stream byte; it is bound to **providers** (transports) at an endpoint. Producers name only the connector. Commands and their answers do **not** use connectors — they use router links (ROUTER.MD). Replaces legacy `sys_data_connector` (which also carried commands).

```
 producers                       connectors (stream byte)         providers @ endpoint
 ─────────                       ────────────────────────         ────────────────────
 ESP_LOG (log capture) ───────▶  0 logs       [0x03] ──────────▶  BLE @ 0xFFE3
 err_report → error sink ─────▶  1 errors     [0x04] ──────────▶  BLE @ 0xFFE3, UART @ 0
 VM subscriptions (later) ────▶  2 telemetry  [0x02] ──────────▶  BLE @ 0xFFE1, UART @ 0
 sys_event NOTIFY events ─────▶  3 events     [0x06] ──────────▶  BLE @ 0xFFE1, UART @ 0
 VM stream block (later) ─────▶  4+ user connectors (router) ──▶  e.g. BLE @ 0xEE01
                                 (router answers [0x05] go to the origin link, not here)
```

## Files

| File | Role |
|---|---|
| `sys_connector.h` | IDs (`//#ref-enum`), stream bytes, provider ops, API, error tags |
| `sys_connector.c` | Registry, providers, bindings, queue + connector task |
| `sys_connector_log.c` | Log capture (`esp_log_set_vprintf` hook) |
| `sys_connector_router.c/.h` | `conn` router (`ROUTER_KEY_CONN` = 0x04) |
| `Kconfig` | Slots, bindings, providers, frame max, queue, task, log line max |

## Model

- **IDs**: `SYS_CONNECTOR_LOGS` 0, `_ERRORS` 1, `_TELEMETRY` 2, `_EVENTS` 3 (system, created by `sys_connector_init`; events = SYS_EVENT.MD notify records, stream 0x06), user from `SYS_CONNECTOR_APP_BASE` 4 to `CONFIG_SYS_CONNECTOR_MAX - 1`.
- **Providers** register under their **transport ID** (`transport_e` in `sys_connector.h`: `TRANSPORT_UART` 1, `TRANSPORT_BLE` 2) — the same ID as the transport's router link. Ops: `send(endpoint, frame, len)`, optional `max_frame(endpoint)`. UART endpoint 0 (`SYS_UART_ENDPOINT`); BLE endpoint = characteristic UUID (any characteristic with TX).
- **Bindings**: up to `CONFIG_SYS_CONNECTOR_BINDINGS` per connector, one per provider (binding again re-addresses).
- **System connectors**: can't be removed and keep their last binding (`ERR_CONN_PROTECTED`; bind the new one first). Suspending them is allowed (free a slow link).

## Sending

`sys_connector_send(id, data, len)` **never blocks**: it copies `[id][stream][payload]` into one ring buffer (`CONFIG_SYS_CONNECTOR_QUEUE_SIZE`) and returns. The connector task (prio 4, below the router tasks) sends `[stream][payload]` to every binding.

- Suspended / no bindings: dropped, NULL. Queue full: `ERR_CONN_QUEUE_FULL` (callers on the log / error path drop it silently).
- Async on purpose: any task can send — one holding a transport's lock, one inside logging — without deadlock or re-entry (the legacy BLE "unlock before logging" problem is gone).
- Per binding the task checks the provider's current limit: too long → `ERR_CONN_TOO_LONG`, not sent there, never cut. `sys_connector_max_payload(id)` gives producers the smallest limit − 1 (calls the providers; not from a task holding a transport lock).
- Errors in the task are **logged only** (`err_log`): reporting them would queue an error record back into this task and could loop.

## Errors stream

`main` registers `err_set_sink(error_sink)`: every `err_report` / `err_report_ex` chain becomes a **router error record** (`router_err_record(origin, chain)`, ROUTER.MD) on connector 1. So command errors (src 1, with link / seq / request) and internal errors (src 0) share one stream — the error history. A client also gets its own command errors in the response; it can skip stream records with its own link + seq.

## Logs stream

`sys_connector_capture_logs(SYS_CONNECTOR_LOGS)`: every ESP_LOG line still goes to the console, and also (ANSI colours and line end stripped, cut to `CONFIG_SYS_CONNECTOR_LOG_LINE_MAX` = 160) to the logs connector. Skipped: lines from ISRs, before the scheduler runs, and from the connector task itself. Formatted on the logging task's stack (line buffer + `vsnprintf`). Log version 1 (`CONFIG_LOG_VERSION_1`): one `vprintf` per line.

## Remote (`sys_connector_router`, root key `ROUTER_KEY_CONN` = 0x04)

| Op | Request | Response |
|---|---|---|
| `0x01` CREATE | `u8 id, u8 stream`, name (NUL-terminated, nothing after the NUL (`cmd_name_arg`), cut to 15) | – |
| `0x02` REMOVE | `u8 id` | – |
| `0x03` BIND | `u8 id, u8 provider, u32 endpoint` | – |
| `0x04` UNBIND | `u8 id, u8 provider` | – |
| `0x05` SUSPEND / `0x06` RESUME | `u8 id` | – |
| `0x07` INFO | – | `u8 count`, per connector `u8 id, u8 stream, u8 flags (bit0 system, bit1 suspended), u8 n, n × {u8 provider, u32 endpoint}, name\0` |

Example — VM temperature stream to a user BLE characteristic (created with the `ble` router, SYS_BLE.MD): `04 01 04 10 74 65 6D 70 00` (CREATE id 4, stream 0x10, "temp"), `04 03 04 02 01 EE 00 00` (BIND id 4 → BLE @ 0xEE01).

## Errors (`ERR_MOD_SYS_CONNECTOR` = 5)

`ERR_CONN_BAD_ID{id}` HIGH, `_NOT_FOUND{id}` MEDIUM, `_EXISTS{id}` LOW, `_PROTECTED{id}` LOW, `_NO_PROVIDER{provider}` MEDIUM, `_BINDINGS_FULL{id}` MEDIUM, `_TOO_LONG{id, provider, len, max}` MEDIUM, `_QUEUE_FULL{id, len}` LOW, `_BAD_ARG` HIGH, `_SEND{id, provider, endpoint}` NONE (context over the provider's error).

## Changes from legacy (`legacy/components/system/sys_data_connector`)

- TX only: no RX bindings, `receive`, `wait_rx`, `notify_rx`, `send_to`, interface connector (commands → router links).
- Async send through one queue + task (legacy sent synchronously in the producer's task).
- Provider IDs = transport IDs (`transport_e`), not Kconfig; connector IDs plain enum values (logs 0, errors 1, telemetry 2, events 3, user from 4; legacy had interface 3, user from 4).
- No per-connector `max_frame`; one frame cap `CONFIG_SYS_CONNECTOR_FRAME_MAX`.
- The error sink and log capture live here / in `main` instead of `runit_board_error_sink_init`.
- Settings class `0x06` → `conn` router.

## Open

- Telemetry producer (VM subscriptions) and a VM stream block are not ported.
- No repeat suppression on the errors stream (legacy counted repeats in a window).
