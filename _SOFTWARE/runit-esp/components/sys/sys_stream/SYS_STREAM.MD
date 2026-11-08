# sys_stream — data streams (for agents)

Component `components/sys/sys_stream/`. A **stream** is a named way for data between the board's code and the outside: the logs, the errors, the telemetry, the events, a user stream, a bridge. Code knows a stream by its **id**, one byte, and a stream has a **direction**, set at create: **OUT** (the board sends: `sys_stream_send`) or **IN** (what comes in on its providers). What goes in goes out to every target of its **one list**; a stream keeps nothing. Commands and their answers never go through streams: they are the session's (SYS_SESSION.MD).

```
 OUT:  sys_stream_send ─┐                                        ┌─► provider (a binding: the payload as is)
                        ├─► the stream queue ─► the stream task ─┼─► peer (a subscriber: PUSH [id][payload], by its session)
 IN:   provider (rx)  ──┘                                        └─► callback (the board's code)
 IN only: also ─► output (another provider: the payload as is)
```

There is no stream both ways, so nothing goes back to where it came from and there is no loop: a device both ways is two streams, one OUT and one IN.

## Files

| File | Role |
|---|---|
| `sys_stream.h` | Use: init, create, remove, mark system, bind, output, remote, unbind, subscribe, unsubscribe, listen, unlisten, send, max payload, is task |
| `sys_stream.c` | The registry, the queue and the task, the targets |
| `config/sys_stream_config.h` | The system streams, the id ranges, the direction, the flags, the target kinds, the state (published to the app) |
| `internal/sys_stream_internal.h` | `sys_stream_info_t` and `sys_stream_list`, LIST's copy of the streams (the router only; not on the include path) |
| `router/sys_stream_router.h` / `.c` | The remote interface (root key `ROUTER_KEY_STREAM` 0x04). The header publishes the ops (`SYS_STREAM_OP_*`, with their sides) and the wire layouts (`sys_stream_cfg_t`, `sys_stream_ref_t`, `sys_stream_binding_t`, LIST's head and target), each op above its frame; `sys_stream.h` only declares `sys_stream_cfg_t` |
| `router/contract_stream.json` | The router's descriptor |
| `errors/` | `ERR_SYS_STREAM_MAP`, its registration, the messages |
| `Kconfig` | The number of streams and targets, the queue, the task |
| `AGENTS.md` | Rules for changing this folder |

## Model

A stream is `sys_stream_cfg_t` `{u8 id, u8 dir, u8 flags}`, set at CREATE and never changed:

- **The id** is the first byte of a PUSH frame's body (SYS_SESSION.MD "Streams"): `0x00` invalid; `0x01`..`0x0F` the board's own streams; `0x10`..`0x7F` user streams; from `0x80` the session's own channels (the watches' entries, the remote devices' events), which no stream takes. CREATE refuses `0x00` and `0x80` or more.
- **The board's own streams**, all OUT, made by the app layer (`runit_board_cfg.c`) and marked with `sys_stream_mark_system` (REMOVE refuses them): `SYS_STREAM_TELEMETRY` 0x02, `_LOGS` 0x03, `_ERRORS` 0x04 (urgent), `_EVENTS` 0x06 (urgent). This component makes no stream itself.
- **`SYS_STREAM_FLAG_URGENT`**: its frames go to the front of every provider's queue.

**The targets** are one list per stream (`CONFIG_SYS_STREAM_TARGETS`), each of one kind, with one `dropped` count:

| Kind | Made by | OUT | IN |
|---|---|---|---|
| **provider** (a binding) | `sys_stream_bind(id, provider)`, BIND | the payload goes there as it is, no frame | what comes in there goes into the stream |
| **peer** (a subscriber) | `sys_stream_subscribe(id, peer)`, SUBSCRIBE (the command's `src`) | `[STREAM][dst peer][src board][seq][id][payload]` through its session | the same |
| **callback** (the board's code) | `sys_stream_listen(id, fn, ctx)` | `fn(ctx, id, payload, len)` on the stream task | the same |
| **remote** | `sys_stream_remote(id, board, provider)`, REMOTE | a data frame through the session (SYS_SESSION.MD "Data to a provider"): that board sends the payload on the provider as it is | the same |
| **output** | `sys_stream_output(id, provider)`, OUTPUT | refused (`ERR_STREAM_DIRECTION`): its bindings are its way out | what came in goes to this provider as it is, no frame |

- An **IN** stream's binding takes the provider's rx hook (`sys_data_provider_open`; SYS_DATA_PROVIDER.MD) at bind and gives it back at unbind or remove; a hook with an owner: `ERR_PROV_IN_USE`. An **OUT** stream's binding holds nothing: it names the provider, each send finds it by its id, and one that is not there counts in the target's `dropped`. So a device both ways on one provider is two streams, one IN and one OUT. Whoever binds answers for what goes there: an OUT stream bound to a session's provider breaks that session (SYS_DATA_PROVIDER.MD "Use").
- A subscriber is a peer id, never this board (`SELF`), `NONE` or `BROADCAST`. The stream does not know whether its way is BLE or UART, or how many boards lie on it: the session routes by `dst` (a master forwards to its slaves and back). A peer that wants another board's stream subscribes there itself. A peer gone (`sys_session_on_gone`) loses its subscriptions.
- A callback copies what it keeps and returns; it never waits (the task carries every stream). **Storage is the consumer's**: the VM latches what it reads, the app keeps what it shows.
- To stop a stream, its targets go (UNSUBSCRIBE, UNBIND).

An output holds nothing, like an OUT stream's binding. **A tunnel between two providers is two IN streams**, one each way: each binds one provider and has the other as its output, so the board joins a BLE characteristic and a raw UART like a cable. **Between two boards** each IN stream has a remote target instead of an output, one stream on each board. A remote target is configuration: unlike a subscriber it stays when its peer is gone (what can't reach it counts in `dropped`), and its messages must fit `CMD_DATA_MAX − 3`. It carries no guarantee: its messages share the stream queue with the logs and the telemetry, and what does not fit is lost; a timing protocol (Modbus RTU) does not survive it.

Examples: a GPS on a raw UART (made with the UART router, SYS_UART.MD "Remote") to the app is an IN stream with an RX binding on that UART and the app subscribed (a bridge); a VM value to a user BLE characteristic is an OUT stream bound to that characteristic.

## Sending

`sys_stream_send(id, data, len)` **never blocks**: it copies `[id][payload]` into one ring buffer (`CONFIG_SYS_STREAM_QUEUE_SIZE`) and returns. What comes in on an IN stream's binding (the driver's RX task) is queued the same way. The stream task (prio 4, below the router tasks) gives each message to every target straight from the queue, then gives its room back; `seq`, the stream's frame counter, goes to its subscribers.

- An IN stream refuses a send: `ERR_STREAM_DIRECTION`. A stream with no target: dropped, NULL. Queue full: `ERR_STREAM_QUEUE_FULL` (callers on the log / error path drop it silently); on an IN stream the message is lost and the subscribers see the gap.
- Any task can send, including one holding a transport's lock or one inside logging, without deadlock or re-entry.
- **The task never waits on a driver**: every send is `NOWAIT` (`sys_data_provider_send`, `sys_session_push`). A provider whose queue is full answers `ERR_PROV_BUSY` at once and the message is lost on **that target only**. A target's `dropped` counts every message not given to it (busy, down, too long, no route); LIST shows it. A subscriber sees a gap in `seq`.
- A payload longer than `CMD_DATA_MAX − 1` (CMD.MD "Frame size") is refused at send (`ERR_STREAM_TOO_LONG`) and dropped when it comes in. That limit is `SYS_STREAM_PAYLOAD_MAX`, the same whatever the stream is bound to. A raw provider's driver refuses a payload its link can't take: not sent there, never cut, counted in the target's `dropped`. Whoever binds a stream to a raw provider sizes its messages for that link.
- Errors in the task are **logged only** (`err_log`): reporting them would queue an error record back into this task and could loop. A target that keeps failing is logged once, then again only after it has taken a message.

## Limits

| Option | Default | Meaning |
|---|---|---|
| `CONFIG_SYS_STREAM_MAX` | 12 | stream slots, the 4 system ones included |
| `CONFIG_SYS_STREAM_TARGETS` | 6 | targets per stream: bindings, subscribers, callbacks |
| `CONFIG_SYS_STREAM_QUEUE_SIZE` | 4608 | bytes of the queue; the message being sent keeps its room until every target has it. A provider's own queue is larger (UART: SYS_UART.MD), so a burst fills this one first and its producer is refused (the VM's sampler resends), not lost unseen |
| `CONFIG_SYS_STREAM_TASK_PRIO` / `_STACK` | 4 / 4096 | the stream task (it holds a whole PUSH frame while it goes to a subscriber) |

## Errors stream

`runit_error_wiring_init()` (`runit_error_policy.c`) registers `error_sink` with `err_set_sink`. Every `err_report` / `err_report_ex` chain goes first to the device error policy, then becomes an **error record** (`cmd_err_record(cmd, chain)`, ERRORS.MD "Error record") on the errors stream (0x04). A response from the policy is logged and sent as a second record. A full queue drops the record. So command errors (src 1, with the sender, seq and request) and internal errors (src 0) share one stream — the error history. A client also gets its own command errors in the answer; it can skip stream records by its own id + seq.

The errors stream does not suppress repeats: every record is sent.

## Logs stream

Filled by `sys_log` (SYS_LOG.MD, `sys_log_init()`, on `SYS_STREAM_LOGS`), which owns the ESP_LOG hook and the line buffer `CONFIG_SYS_LOG_LINE_MAX`. The board config binds it to the BLE LOGS characteristic (the lines as they are); a console client subscribes to it. The stream only exports `sys_stream_is_task()` so the hook can skip the stream task's own lines.

## Remote (`sys_stream_router`, root key `ROUTER_KEY_STREAM` = 0x04)

Requests are `[0x04][dir][op][args]` (`dir` 0 read, 1 write; ROUTER.MD). The ops and the layouts are in `sys_stream_router.h`; `contract_stream.json` names them. LIST is a read, the other ops writes. A provider's id is one byte (SYS_DATA_PROVIDER.MD "The id"). Codes 0x05, 0x06, 0x08 and 0x0B are unused.

| Op | Request | Response |
|---|---|---|
| `0x01` CREATE | `sys_stream_cfg_t`: `u8 id, u8 dir, u8 flags` | – |
| `0x02` REMOVE | `sys_stream_ref_t` `{u8 id}` | – |
| `0x03` BIND | `sys_stream_binding_t` `{u8 id, u8 provider}` | – |
| `0x0C` OUTPUT | `sys_stream_binding_t` `{u8 id, u8 provider}` | – (an IN stream's way out) |
| `0x0D` REMOTE | `sys_stream_remote_t` `{u8 id, u8 board, u8 provider}` | – (not this board: `ERR_STREAM_BAD_ARG`) |
| `0x04` UNBIND | `sys_stream_binding_t`, a binding, an output or a remote target of this provider, on any board | – |
| `0x0E` UNBIND_ALL | `sys_stream_ref_t`: every binding, output and remote target | – |
| `0x07` LIST | – | `u8 count`, per stream its `sys_stream_head_t` `{cfg (u8 id, u8 dir, u8 flags), u8 state, u8 targets}`, then its `sys_stream_target_t` `{u8 type, u8 peer, u8 provider, u16 dropped}` (5 B each; `peer` for a peer or a remote target, `provider` for a binding, an output or a remote target, else zero) |
| `0x09` SUBSCRIBE | `sys_stream_ref_t` | – (the command's `src` gets the stream's messages) |
| `0x0A` UNSUBSCRIBE | `sys_stream_ref_t` | – (of the command's `src`) |

Example — a tunnel between a BLE characteristic (provider 0x11) and a raw UART (provider 0x10): `04 01 01 20 01 00`, `04 01 03 20 11`, `04 01 0C 20 10` (stream 0x20, IN, from the characteristic to the UART), then `04 01 01 21 01 00`, `04 01 03 21 10`, `04 01 0C 21 11` (0x21, the other way).

Example — board 2's logs on a BLE characteristic of board 1 (its provider 0x11), sent to board 2: `04 01 0D 03 01 11` (stream 0x03, board 1, provider 0x11).

Example — a VM value to a user BLE characteristic (created with the `ble` router, SYS_BLE.MD): `04 01 01 10 00 00` (CREATE 0x10, OUT), `04 01 03 10 11` (BIND 0x10 → provider 0x11, the id its CHAR_CREATE gave it). The app's own streams: `04 01 09 04` (SUBSCRIBE errors) once its session opens.

## Errors (`ERR_MOD_SYS_STREAM` = 5)

`ERR_STREAM_NOT_FOUND{stream}`, `_EXISTS{stream}`, `_SYSTEM{stream}` (REMOVE of a system stream), `_TARGETS_FULL{stream}`, `_TOO_LONG{stream, len, max}`, `_QUEUE_FULL{stream, len}`, `_BAD_ARG`, `_FULL{stream}` (`CONFIG_SYS_STREAM_MAX`), `_DIRECTION{stream}` (a send to an IN stream). A provider's own failure is its `ERR_PROV_*`; a subscriber's the session's (`ERR_SESSION_NO_ROUTE`).

## Needs elsewhere

- **sys_data_provider**: an IN binding's open and close, send `NOWAIT`.
- **sys_session**: `sys_session_push` to a subscriber, `sys_session_data` to a remote target, `sys_session_on_gone`, `sys_session_no_wait` for the task.
- **router**: `router_origin` for the subscriber of SUBSCRIBE.

## Open

- No callback in the firmware yet: the VM's input slots, a VM listener per stream the program reads, wait for the VM's stream blocks.
