# sys_data_connector — outbound data streams (for agents)

Component `components/sys/sys_data_connector/`. A **connector** is a named stream of data from the board's code to the outside: logs, errors, telemetry, events, a user stream. Code knows a connector by its **id**, one byte, and sends to it; producers never block. A connector has two kinds of **targets**, and what a frame looks like follows from the target:

| Target | Made by | A frame goes as |
|---|---|---|
| a **binding** to a provider (SYS_DATA_PROVIDER.MD) | BIND (the board config, the app) | the payload as is: the provider is the connector's alone, so it names the stream |
| a **subscriber**: a peer (the app, a test host) | SUBSCRIBE, by the peer itself | `[connector][payload]` in a session's STREAM frame (SYS_SESSION.MD "Streams"): the byte tells the peer's streams apart |

Commands and their answers do not go through connectors: they are the session's, which opens its own provider.

```
 producers                       connector                        targets
 ─────────                       ─────────                        ───────
 ESP_LOG (log capture) ───────▶  0x03 logs      ──────────────▶  binding: BLE LOGS 0xFFE3; subscribers
 err_report → error sink ─────▶  0x04 errors    ──────────────▶  subscribers
 VM subscriptions ────────────▶  0x02 telemetry ──────────────▶  subscribers
 sys_event NOTIFY events ─────▶  0x06 events    ──────────────▶  subscribers
 sys_event, all but LOCAL ────▶  0x07 events all ──────────────▶  subscribers; another board's mirror
                                 user connectors (router) ─────▶  e.g. 0x10 → a user characteristic
```

## Files

| File | Role |
|---|---|
| `sys_data_connector.h` | The API for the rest of the firmware: init, create, remove, bind, subscribe, unsubscribe, mirror, unmirror, send, max payload, is-task (includes `sys_data_connector_config.h`) |
| `internal/sys_data_connector_internal.h` | What only the component's router uses: unbind, suspend, the state for LIST and its packed layouts (`PRIV_INCLUDE_DIRS`) |
| `config/sys_data_connector_config.h` | The system connectors (`SYS_DATA_CONNECTOR_*`, group `connector`), a binding's flags (`conn_bind`), a connector's flags (`conn_flag`) and state (`conn_state`): `//#ref-defines`, wire format |
| `errors/sys_data_connector_errors.h` | `ERR_SYS_DATA_CONNECTOR_MAP`, the module's error tags (included by `sys_data_connector.h`) |
| `errors/sys_data_connector_errors.c` | `ERR_MODULE_DEFINE`: the tags registered (the component is linked `WHOLE_ARCHIVE`, since nothing references this file) |
| `errors/errors_sys_data_connector.json` | The error messages |
| `sys_data_connector.c` | Registry, bindings, subscribers, queue + connector task |
| `router/sys_data_connector_router.c/.h` | `conn` router (`ROUTER_KEY_CONN` = 0x04) |
| `router/contract_connector.json` | The router's descriptor |
| `Kconfig` | Slots, bindings, subscribers, queue, task |

## Model

- **Connectors**: a connector is one byte. System (created by `sys_data_connector_init`, state `SYSTEM`): `SYS_DATA_CONNECTOR_TELEMETRY` 0x02, `_LOGS` 0x03, `_ERRORS` 0x04 (urgent), `_EVENTS` 0x06 (urgent; SYS_EVENT.MD notify records), `_EVENTS_ALL` 0x07 (every event but the local ones). A user connector (CREATE `u8 id, u8 flags`) takes any other byte but 0x00 (`ERR_CONN_BAD_ARG`) and 0x05 (`SYS_DATA_CONNECTOR_WATCHES`: the byte of the watches' entries in a STREAM frame to their owner, SYS_WATCH.MD); a byte that has a connector already, or 0x05: `ERR_CONN_EXISTS`. The registry is an array of `CONFIG_SYS_DATA_CONNECTOR_MAX` connectors kept packed (a removed one takes the last one's place), the five system ones included; a CREATE with none free: `ERR_CONN_FULL`. System connectors can't be removed (`ERR_CONN_SYSTEM`); suspending them is allowed.
- **Flags**: `SYS_DATA_CONNECTOR_FLAG_URGENT`: every send of its frames goes to the front of the provider's queue (`SYS_DATA_PROVIDER_SEND_URGENT`, the STREAM frame's URGENT flag).
- **Bindings**: `sys_data_connector_bind(id, provider, SYS_DATA_CONNECTOR_BIND_TX)`, up to `CONFIG_SYS_DATA_CONNECTOR_BINDINGS` per connector. The binding opens the provider as its **one user** (SYS_DATA_PROVIDER.MD "Use"): no one else sends on it, so bytes of two senders never mix; a provider another user has (a session's: the runIT BLE pair, the UART): `ERR_PROV_IN_USE`. Bind again on the same provider: only its flags change. Unbind and remove close the providers; UNBIND of provider link 0 drops every binding of the connector. Only TX: a connector keeps nothing that comes in (`ERR_CONN_BAD_ARG` for other flags).
- **Subscribers**: a peer subscribes itself: SUBSCRIBE `u8 id`, and the command's `src` (`router_origin()`) is the subscriber, so no peer names another. From C `sys_data_connector_subscribe(id, peer)`. SUBSCRIBE of the same peer again gives one subscription, so a client that opened its session again and subscribes again gets one stream. Up to `CONFIG_SYS_DATA_CONNECTOR_SUBSCRIBERS` per connector (`ERR_CONN_TARGETS_FULL`); the board itself, NONE or BROADCAST: `ERR_CONN_BAD_ARG`. A subscription ends with UNSUBSCRIBE, or when the peer is gone: the connectors listen for gone peers (`sys_session_on_gone`) and drop its subscriptions.
- **Mirrors**: a connector can be the mirror of a connector of another board (below).
- **Telemetry** is produced by the VM subscriptions (`vm_sub.c`).

## Mirrors

A **mirror** is this board's connector for a connector of another board: what that board sends there comes in here, and goes on to every target of the mirror. So any number of this board's users (sys_remote for the events of its remote devices, a binding, the app subscribed to the mirror) get one stream of another board, which crosses the link once.

- `sys_data_connector_mirror(id, board, remote_id, fn, ctx)`: makes the connector `id` (a user connector created before, not a system one, not a mirror yet) the mirror of `remote_id` on `board` (not 0 or 0x05), and subscribes this board there (SUBSCRIBE, a call: SYS_SESSION.MD "The board's own calls"). `fn` (or NULL) is told of each frame. A mirror keeps nothing: the frame goes to `fn` and the targets, and is gone.
- A STREAM frame `[remote_id][payload]` from `board` (the session's stream listener, SYS_SESSION.MD "Streams") is queued with the connector's own frames, `[0][mirror id][board][seq u16][payload]` (0 is no connector), so the connector task runs `fn(ctx, board, payload, len, missed)` and then sends the payload to every target of the mirror as if it had been sent to it. `fn` runs on the connector task: short, never blocks. A full queue drops the frame on the RX task, and the next frame's `seq` shows it.
- A gap in the frames' `seq` is frames lost on the way: `missed` is the gap, and `ERR_CONN_MIRROR_LOST{connector, board, count}` is logged, once per gap.
- The board gone (`sys_session_on_gone`): the mirror waits, its `seq` check starts again; reached again (`sys_session_on_reached`): it subscribes again. A subscription on the other board ends with its session, so a restarted board needs this.
- `sys_data_connector_unmirror(id)`: UNSUBSCRIBE there; the connector stays, as an ordinary one.

## Sending

`sys_data_connector_send(id, data, len)` **never blocks**: it copies `[connector][payload]` into one ring buffer (`CONFIG_SYS_DATA_CONNECTOR_QUEUE_SIZE`) and returns. The connector task (prio 4, below the router tasks) sends it to every target straight from the queue (a binding gets the payload, a subscriber the whole, `seq` the connector's frame counter), then gives its room back.

- Suspended / no target: dropped, NULL. Queue full: `ERR_CONN_QUEUE_FULL` (callers on the log / error path drop it silently).
- Any task can send, including one holding a transport's lock or one inside logging, without deadlock or re-entry.
- **The task never waits on a driver**: every send is `NOWAIT` (`sys_data_provider_send`, `sys_session_stream`). A provider whose queue is full answers `ERR_PROV_BUSY` at once and the frame is lost on **that target only**; the other targets and connectors go on. A target's `dropped` counts every frame not sent to it (busy, down, too long, no route); LIST shows it. A subscriber sees a gap in `seq`.
- A payload longer than `CMD_DATA_MAX − 1` (CMD.MD "Frame size") is refused at send (`ERR_CONN_TOO_LONG`). Per binding the provider checks its own `max_frame`: too long, `ERR_PROV_TOO_LONG`, not sent there, never cut. `sys_data_connector_max_payload(id)` gives producers `CMD_DATA_MAX − 1`, less when a bound provider that is up takes less (an unframed one); any task may call it.
- Errors in the task are **logged only** (`err_log`): reporting them would queue an error record back into this task and could loop. A target that keeps failing is logged once, then again only after it has taken a frame.

## Limits

| Option | Default | Meaning |
|---|---|---|
| `CONFIG_SYS_DATA_CONNECTOR_MAX` | 16 | connector slots, the 5 system ones and the mirrors included (sys_remote makes one per board that has a remote device here) |
| `CONFIG_SYS_DATA_CONNECTOR_BINDINGS` | 3 | bindings per connector |
| `CONFIG_SYS_DATA_CONNECTOR_SUBSCRIBERS` | 4 | subscribers per connector |
| `CONFIG_SYS_DATA_CONNECTOR_QUEUE_SIZE` | 4608 | bytes of the send queue; the frame being sent keeps its room until every target has it. A provider's own queue is larger (UART: SYS_UART.MD), so a burst fills this one first and its producer is refused (the VM's sampler resends), not lost unseen |
| `CONFIG_SYS_DATA_CONNECTOR_TASK_PRIO` / `_STACK` | 4 / 4096 | the connector task (it holds a whole stream frame while it goes to a subscriber) |

## Errors stream

`runit_error_wiring_init()` (`runit_error_policy.c`) registers `error_sink` with `err_set_sink`. Every `err_report` / `err_report_ex` chain goes first to the device error policy, then becomes an **error record** (`err_record(cmd, chain)`, ERRORS.MD "Error record") on the errors stream (0x04). A response from the policy is logged and sent as a second record. A full queue drops the record. So command errors (src 1, with the sender, seq and request) and internal errors (src 0) share one stream — the error history. A client also gets its own command errors in the answer; it can skip stream records by its own id + seq.

The errors stream does not suppress repeats: every record is sent.

## Logs stream

Filled by `sys_log` (SYS_LOG.MD, `sys_log_init()`, on `SYS_DATA_CONNECTOR_LOGS`), which owns the ESP_LOG hook and the line buffer `CONFIG_SYS_LOG_LINE_MAX`. The board config binds it to the BLE LOGS characteristic (the lines as they are); a console client subscribes to it. The connector only exports `sys_data_connector_is_task()` so the hook can skip the connector task's own lines.

## Remote (`sys_data_connector_router`, root key `ROUTER_KEY_CONN` = 0x04)

Requests are `[0x04][dir][op][args]` (`dir` 0 read, 1 write; ROUTER.MD). LIST is a read, the other ops writes. A provider id is `{u8 link, u32 channel}` (SYS_DATA_PROVIDER.MD "The id").

| Op | Request | Response |
|---|---|---|
| `0x01` CREATE | `u8 id, u8 flags` | – |
| `0x02` REMOVE | `u8 id` | – |
| `0x03` BIND | `u8 id`, provider id, `u8 flags` (`BIND_TX`) | – |
| `0x04` UNBIND | `u8 id`, provider id (link 0: every binding) | – |
| `0x05` SUSPEND / `0x06` RESUME | `u8 id` | – |
| `0x07` LIST | – | `u8 count`, per connector its head `{u8 id, u8 flags, u8 state, u8 subscribers, u8 bindings}`, its subscribers `{u8 peer, u16 dropped}`, its bindings `{provider id, u8 flags, u16 dropped}` |
| `0x09` SUBSCRIBE | `u8 id` | – (the command's `src` gets the connector's frames) |
| `0x0A` UNSUBSCRIBE | `u8 id` | – (of the command's `src`) |

`0x08` is kept for a READ of a connector's received data.

Example — VM temperature stream to a user BLE characteristic (created with the `ble` router, SYS_BLE.MD): `04 01 01 10 00` (CREATE 0x10), `04 01 03 10 02 01 EE 00 00 02` (BIND 0x10 → BLE 0xEE01, TX). The app's own streams: `04 01 09 04` (SUBSCRIBE errors) once its session opens.

## Errors (`ERR_MOD_SYS_DATA_CONNECTOR` = 5)

Every tag but `BAD_ARG` carries the connector's id; codes 1, 5, 10, 12 and 13 are unused. A provider's failure (open, send) is its own `ERR_PROV_*`, which names it.

`ERR_CONN_NOT_FOUND{connector}` MEDIUM, `_EXISTS{connector}` LOW, `_SYSTEM{connector}` LOW (remove: a system connector), `_TARGETS_FULL{connector}` MEDIUM (bind, subscribe), `_TOO_LONG{connector, len, max}` MEDIUM (send: past `CMD_DATA_MAX − 1`), `_QUEUE_FULL{connector, len}` LOW, `_BAD_ARG` HIGH, `_FULL{connector}` MEDIUM, `_MIRROR_LOST{connector, board, count}` MEDIUM (a mirror's frames lost on the way; logged).
