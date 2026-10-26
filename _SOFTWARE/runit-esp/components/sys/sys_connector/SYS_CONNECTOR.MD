# sys_connector — outbound data streams (for agents)

Component `components/sys/sys_connector/`. A **connector** is a logical outbound stream, named by one byte that is also the first byte of every frame it sends (its stream byte). It is bound to **providers** (transports) at an **endpoint**. Three numbers, then: the connector, the provider, the endpoint. Producers name only the connector. TX only: commands and their answers use router links (ROUTER.MD), not connectors.

```
 producers                       connector                        provider @ endpoint
 ─────────                       ─────────                        ───────────────────
 ESP_LOG (log capture) ───────▶  0x03 logs      ──────────────▶  BLE @ 0xFFE3
 err_report → error sink ─────▶  0x04 errors    ──────────────▶  BLE @ 0xFFE3, UART @ 0
 VM subscriptions ────────────▶  0x02 telemetry ──────────────▶  BLE @ 0xFFE1, UART @ 0
 sys_event NOTIFY events ─────▶  0x06 events    ──────────────▶  BLE @ 0xFFE1, UART @ 0
                                 user connectors (router) ─────▶  e.g. 0x10 → BLE @ 0xEE01
                                 (router answers 0x05 go to the origin link, not here)
```

## Files

| File | Role |
|---|---|
| `sys_connector.h` | Provider ops, API (includes `sys_connector_config.h`) |
| `config/sys_connector_config.h` | Transport IDs (`transport_e`), the system connectors (`SYS_CONNECTOR_*`, group `connector`; the router's answer byte is `CONFIG_ROUTER_RESPONSE_STREAM`): `//#ref-enum` / `//#ref-defines`, wire format |
| `errors/sys_connector_errors.h` | `ERR_SYS_CONNECTOR_MAP`, the module's error tags (included by `sys_connector.h`) |
| `errors/sys_connector_errors.c` | `ERR_MODULE_DEFINE`: the tags registered (the component is linked `WHOLE_ARCHIVE`, since nothing references this file) |
| `errors/errors_sys_connector.json` | The error messages |
| `sys_connector.c` | Registry (slots found by connector), providers, bindings, queue + connector task |
| `router/sys_connector_router.c/.h` | `conn` router (`ROUTER_KEY_CONN` = 0x04) |
| `Kconfig` | Slots, bindings, providers, frame max, queue, task |

## Model

- **Connectors**: a connector is one byte, the only number it has, and every frame it sends starts with it. System (created by `sys_connector_init`, flag `system`): `SYS_CONNECTOR_TELEMETRY` 0x02, `_LOGS` 0x03, `_ERRORS` 0x04, `_EVENTS` 0x06 (SYS_EVENT.MD notify records). A user connector (CREATE) takes any other byte except the router's answer byte (`CONFIG_ROUTER_RESPONSE_STREAM`, 0x05 by default: `ERR_CONN_BAD_ARG`); a byte that has a connector already: `ERR_CONN_EXISTS`. One byte, one connector, so a receiver always tells the streams apart. The registry is an array of `CONFIG_SYS_CONNECTOR_MAX` connectors kept packed (a removed one takes the last one's place), the four system ones included; a CREATE with none free: `ERR_CONN_FULL`.
- **Providers** register under their **transport ID** (`transport_e` in `sys_connector_config.h`: `TRANSPORT_UART` 1, `TRANSPORT_BLE` 2; 3 is kept for MQTT over Wi-Fi, listed once it has a provider, since the app offers every listed transport) — the same ID as the transport's router link. Ops: `send(endpoint, frame, len)`, optional `max_frame(endpoint)`. UART endpoint 0 (`SYS_UART_ENDPOINT`); BLE endpoint = characteristic UUID (any characteristic of the BLE link).
- **Frame cap**: one cap for every connector, `CONFIG_SYS_CONNECTOR_FRAME_MAX`. A provider's `max_frame` can lower it per endpoint; there is no per-connector cap.
- **Bindings**: up to `CONFIG_SYS_CONNECTOR_BINDINGS` per connector, one per provider (binding again re-addresses).
- **System connectors**: can't be removed and keep their last binding (`ERR_CONN_PROTECTED`; bind the new one first). Suspending them is allowed (free a slow link).
- **Telemetry** is produced by the VM subscriptions (`vm_sub.c`).

## Sending

`sys_connector_send(connector, data, len)` **never blocks**: it copies `[connector][payload]`, the frame itself, into one ring buffer (`CONFIG_SYS_CONNECTOR_QUEUE_SIZE`) and returns. The connector task (prio 4, below the router tasks) sends that frame to every binding straight from the queue, then gives its room back.

- Suspended / no bindings: dropped, NULL. Queue full: `ERR_CONN_QUEUE_FULL` (callers on the log / error path drop it silently).
- Any task can send, including one holding a transport's lock or one inside logging, without deadlock or re-entry.
- Per binding the task checks the provider's current limit: too long → `ERR_CONN_TOO_LONG`, not sent there, never cut. `sys_connector_max_payload(connector)` gives producers the smallest limit − 1 (calls the providers; not from a task holding a transport lock).
- Errors in the task are **logged only** (`err_log`): reporting them would queue an error record back into this task and could loop. A provider that keeps failing (its queue full) is logged once, then again only after it has sent a frame.
- `send` to a connector that doesn't exist fails with `ERR_CONN_NOT_FOUND`. A payload longer than `CONFIG_SYS_CONNECTOR_FRAME_MAX - 1` is refused at `send` (`ERR_CONN_TOO_LONG`, provider 0), before it is queued.

## Adding a transport

A transport is a router link and a connector provider under one ID (`transport_e`, `sys_connector_config.h`). Its provider side is one `sys_connector_provider_t`:

- `send(endpoint, frame, len)`: hands one frame (stream byte included) to the transport's own TX queue and returns; the transport's own task puts it on the wire (UART: its TX task; BLE: the characteristic's TX buffer and the BLE task). It never waits for the line itself, only for room in that queue, bounded (UART 1 s, BLE 20 ms): a full queue holds this task, so its own queue fills and producers are refused rather than frames lost unseen. Runs only on the connector task.
- `max_frame(endpoint)`: the largest frame that endpoint takes now, or NULL for `CONFIG_SYS_CONNECTOR_FRAME_MAX`. Asked by `sys_connector_max_payload`, which any task may call except one holding this transport's lock.
- `endpoint` is the transport's own address: UART uses `SYS_UART_ENDPOINT` (0), BLE uses the characteristic UUID (`SYS_BLE_CHR_RUNIT_TX`, `SYS_BLE_CHR_RUNIT_LOGS`, or a user one).

Register it at boot with `sys_connector_register_provider(TRANSPORT_…, &provider)`, before the bindings. The ID must be in `1..CONFIG_SYS_CONNECTOR_PROVIDERS_MAX`; a new transport gets its ID in `transport_e`, and the `transport_e` change goes through `templates/` (AGENTS.md). Then bind connectors to it with `sys_connector_bind`.

## Limits

| Option | Default | Meaning |
|---|---|---|
| `CONFIG_SYS_CONNECTOR_MAX` | 8 | connector slots, the 4 system ones included |
| `CONFIG_SYS_CONNECTOR_BINDINGS` | 3 | bindings per connector |
| `CONFIG_SYS_CONNECTOR_PROVIDERS_MAX` | 4 | provider IDs |
| `CONFIG_SYS_CONNECTOR_FRAME_MAX` | 512 | largest frame, stream byte included |
| `CONFIG_SYS_CONNECTOR_QUEUE_SIZE` | 4608 | bytes of the send queue; the frame being sent keeps its room until every binding has it |
| `CONFIG_SYS_CONNECTOR_TASK_PRIO` / `_STACK` | 4 / 4096 | the connector task |

## Diagnostics

`sys_connector_list(out)` copies every connector's state (`sys_connector_info_t`: the connector, its flags and bindings) under one lock: the router's INFO (below), which the app reads to bring the board's streams to the project's. `sys_connector_exists(connector)` only says whether it exists.

## Errors stream

`runit_error_wiring_init()` (`runit_error_policy.c`) registers `error_sink` with `err_set_sink`. Every `err_report` / `err_report_ex` chain goes first to the device error policy, then becomes an **error record** (`err_record(cmd, chain)`, ERRORS.MD "Error record") on the errors stream (0x04). A response from the policy is logged and sent as a second record. A full queue drops the record. So command errors (src 1, with link / seq / request) and internal errors (src 0) share one stream — the error history. A client also gets its own command errors in the response; it can skip stream records with its own link + seq.

The errors stream does not suppress repeats: every record is sent.

## Logs stream

Filled by `sys_log` (SYS_LOG.MD, `sys_log_init(SYS_CONNECTOR_LOGS)`), which owns the ESP_LOG hook and the line buffer `CONFIG_SYS_LOG_LINE_MAX`. The connector only exports `sys_connector_is_task()` so the hook can skip the connector task's own lines.

## Remote (`sys_connector_router`, root key `ROUTER_KEY_CONN` = 0x04)

Requests are `[0x04][dir][op][args]` (`dir` 0 read, 1 write; ROUTER.MD). INFO is a read, the other ops writes.

| Op | Request | Response |
|---|---|---|
| `0x01` CREATE | `u8 connector` | – |
| `0x02` REMOVE | `u8 connector` | – |
| `0x03` BIND | `u8 connector, u8 provider, u32 endpoint` | – |
| `0x04` UNBIND | `u8 connector, u8 provider` | – |
| `0x05` SUSPEND / `0x06` RESUME | `u8 connector` | – |
| `0x07` INFO | – | `u8 count`, per connector `u8 connector, u8 flags (bit0 system, bit1 suspended), u8 n, n × {u8 provider, u32 endpoint}` |

Example — VM temperature stream to a user BLE characteristic (created with the `ble` router, SYS_BLE.MD): `04 01 01 10` (CREATE 0x10), `04 01 03 10 02 01 EE 00 00` (BIND 0x10 → BLE @ 0xEE01).

## Errors (`ERR_MOD_SYS_CONNECTOR` = 5)

`ERR_CONN_NOT_FOUND{connector}` MEDIUM, `_EXISTS{connector}` LOW, `_PROTECTED{connector}` LOW, `_NO_PROVIDER{provider}` MEDIUM, `_BINDINGS_FULL{connector}` MEDIUM, `_TOO_LONG{connector, provider, len, max}` MEDIUM, `_QUEUE_FULL{connector, len}` LOW, `_BAD_ARG` HIGH, `_SEND{connector, provider, endpoint}` NONE (context over the provider's error), `_FULL{connector}` MEDIUM (code 11; code 1 is unused).
