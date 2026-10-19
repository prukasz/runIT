# sys_connector — outbound data streams (for agents)

Component `components/sys/sys_connector/`. A **connector** is a logical outbound stream with a stream byte; it is bound to **providers** (transports) at an endpoint. Producers name only the connector. TX only: commands and their answers use router links (ROUTER.MD), not connectors.

```
 producers                       connectors (stream byte)         providers @ endpoint
 ─────────                       ────────────────────────         ────────────────────
 ESP_LOG (log capture) ───────▶  0 logs       [0x03] ──────────▶  BLE @ 0xFFE3
 err_report → error sink ─────▶  1 errors     [0x04] ──────────▶  BLE @ 0xFFE3, UART @ 0
 VM subscriptions ────────────▶  2 telemetry  [0x02] ──────────▶  BLE @ 0xFFE1, UART @ 0
 sys_event NOTIFY events ─────▶  3 events     [0x06] ──────────▶  BLE @ 0xFFE1, UART @ 0
                                 user connectors 4+ (router) ──▶  e.g. BLE @ 0xEE01
                                 (router answers [0x05] go to the origin link, not here)
```

## Files

| File | Role |
|---|---|
| `sys_connector.h` | IDs (`//#ref-enum`), stream bytes, provider ops, API, error tags |
| `sys_connector.c` | Registry, providers, bindings, queue + connector task |
| `sys_connector_router.c/.h` | `conn` router (`ROUTER_KEY_CONN` = 0x04) |
| `Kconfig` | Slots, bindings, providers, frame max, queue, task |

## Model

- **IDs**: `SYS_CONNECTOR_LOGS` 0, `_ERRORS` 1, `_TELEMETRY` 2, `_EVENTS` 3 (system, created by `sys_connector_init`; events = SYS_EVENT.MD notify records, stream 0x06), user from `SYS_CONNECTOR_APP_BASE` 4 to `CONFIG_SYS_CONNECTOR_MAX - 1`.
- **Providers** register under their **transport ID** (`transport_e` in `sys_connector.h`: `TRANSPORT_UART` 1, `TRANSPORT_BLE` 2) — the same ID as the transport's router link. Ops: `send(endpoint, frame, len)`, optional `max_frame(endpoint)`. UART endpoint 0 (`SYS_UART_ENDPOINT`); BLE endpoint = characteristic UUID (any characteristic of the BLE link).
- **Frame cap**: one cap for every connector, `CONFIG_SYS_CONNECTOR_FRAME_MAX`. A provider's `max_frame` can lower it per endpoint; there is no per-connector cap.
- **Stream bytes**: a user connector can't take the system stream bytes (telemetry 0x02, logs 0x03, errors 0x04, events 0x06) or the router's answer byte (`CONFIG_ROUTER_RESPONSE_STREAM`, 0x05 by default). CREATE with one of them fails with `ERR_CONN_BAD_ARG`. Two user connectors may share a byte; the stream doesn't tell them apart.
- **Bindings**: up to `CONFIG_SYS_CONNECTOR_BINDINGS` per connector, one per provider (binding again re-addresses).
- **System connectors**: can't be removed and keep their last binding (`ERR_CONN_PROTECTED`; bind the new one first). Suspending them is allowed (free a slow link).
- **Telemetry** is produced by the VM subscriptions (`vm_sub.c`).

## Sending

`sys_connector_send(id, data, len)` **never blocks**: it copies `[id][stream][payload]` into one ring buffer (`CONFIG_SYS_CONNECTOR_QUEUE_SIZE`) and returns. The connector task (prio 4, below the router tasks) sends `[stream][payload]` to every binding.

- Suspended / no bindings: dropped, NULL. Queue full: `ERR_CONN_QUEUE_FULL` (callers on the log / error path drop it silently).
- Any task can send, including one holding a transport's lock or one inside logging, without deadlock or re-entry.
- Per binding the task checks the provider's current limit: too long → `ERR_CONN_TOO_LONG`, not sent there, never cut. `sys_connector_max_payload(id)` gives producers the smallest limit − 1 (calls the providers; not from a task holding a transport lock).
- Errors in the task are **logged only** (`err_log`): reporting them would queue an error record back into this task and could loop.
- `send` to a connector that doesn't exist fails with `ERR_CONN_NOT_FOUND`. A payload longer than `CONFIG_SYS_CONNECTOR_FRAME_MAX - 1` is refused at `send` (`ERR_CONN_TOO_LONG`, provider 0), before it is queued.

## Adding a transport

A transport is a router link and a connector provider under one ID (`transport_e`, `sys_connector.h`). Its provider side is one `sys_connector_provider_t`:

- `send(endpoint, frame, len)`: sends one frame, stream byte included. Runs only on the connector task.
- `max_frame(endpoint)`: the largest frame that endpoint takes now, or NULL for `CONFIG_SYS_CONNECTOR_FRAME_MAX`. Asked by `sys_connector_max_payload`, which any task may call except one holding this transport's lock.
- `endpoint` is the transport's own address: UART uses `SYS_UART_ENDPOINT` (0), BLE uses the characteristic UUID (`SYS_BLE_CHR_RUNIT_TX`, `SYS_BLE_CHR_RUNIT_LOGS`, or a user one).

Register it at boot with `sys_connector_register_provider(TRANSPORT_…, &provider)`, before the bindings. The ID must be in `1..CONFIG_SYS_CONNECTOR_PROVIDERS_MAX`; a new transport gets its ID in `transport_e`, and the `transport_e` change goes through `templates/` (AGENTS.md). Then bind connectors to it with `sys_connector_bind`.

## Limits

| Option | Default | Meaning |
|---|---|---|
| `CONFIG_SYS_CONNECTOR_MAX` | 8 | connector IDs `0..MAX-1` |
| `CONFIG_SYS_CONNECTOR_BINDINGS` | 3 | bindings per connector |
| `CONFIG_SYS_CONNECTOR_PROVIDERS_MAX` | 4 | provider IDs |
| `CONFIG_SYS_CONNECTOR_FRAME_MAX` | 512 | largest frame, stream byte included |
| `CONFIG_SYS_CONNECTOR_QUEUE_SIZE` | 4096 | bytes of the send queue |
| `CONFIG_SYS_CONNECTOR_TASK_PRIO` / `_STACK` | 4 / 4096 | the connector task |

## Diagnostics

`sys_connector_info(id, out)` gives one connector's state and bindings; the router's INFO (below) lists them all.

## Remote (`sys_connector_router`, root key `ROUTER_KEY_CONN` = 0x04)

## Errors stream

`runit_error_wiring_init()` (`runit_error_policy.c`) registers `error_sink` with `err_set_sink`. Every `err_report` / `err_report_ex` chain goes first to the device error policy, then becomes a **router error record** (`router_err_record(origin, chain)`, ROUTER.MD) on connector 1. A response from the policy is logged and sent as a second record. A full queue drops the record. So command errors (src 1, with link / seq / request) and internal errors (src 0) share one stream — the error history. A client also gets its own command errors in the response; it can skip stream records with its own link + seq.

The errors stream does not suppress repeats: every record is sent.

## Logs stream

Filled by `sys_log` (SYS_LOG.MD, `sys_log_init(SYS_CONNECTOR_LOGS)`), which owns the ESP_LOG hook and the line buffer `CONFIG_SYS_LOG_LINE_MAX`. The connector only exports `sys_connector_is_task()` so the hook can skip the connector task's own lines.

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
