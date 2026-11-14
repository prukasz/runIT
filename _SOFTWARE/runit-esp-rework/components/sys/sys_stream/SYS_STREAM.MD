# sys_stream — data streams

Component `runit-esp-rework/components/sys/sys_stream/`. A **stream** is a named way for data between the board's code and the outside: the logs, the errors, the telemetry, the events, a user stream, a bridge or tunnel. Code knows a stream by its **id**, one byte; every stream feeds one list of targets, and a stream keeps nothing. Commands and their answers never go through streams: they are the session's (`SYS_SESSION.MD`).

```
  sys_stream_send ────────┐                                        ┌─► SINK {board, provider}: direct send on this board, DATA frame to another
                          ├─► the stream queue ─► the stream task ─┼─► PEER (subscriber): PUSH [id][payload], by its session
  SOURCE {provider} (rx) ─┘                                        └─► CALLBACK (the board's code)
```

## Files

| File | Role |
|---|---|
| `sys_stream.h` | Use: init, create, remove, mark system, source, sink, unbind, subscribe, unsubscribe, listen, unlisten, send, max payload, is task |
| `sys_stream.c` | The registry, the queue and the task, delivery to the 4 target kinds; `ERR_MODULE_DEFINE`, `MODULE_ID_DEFINE(STREAM, 0x29)` |
| `config/sys_stream_config.h` | The system streams, the id ranges, the flags, `sys_stream_target_e`, the state (published to the app) |
| `internal/sys_stream_internal.h` | `sys_stream_info_t` and `sys_stream_list`, LIST's copy of the streams (router only; not on include path) |
| `router/sys_stream_router.h` / `.c` | The remote interface (`MODULE_ID_STREAM` 0x29, `STREAM_OPERATIONS`); registers itself via `ROUTER_MODULE_REGISTER` |
| `router/contract_stream.json` | The router's descriptor |
| `errors/` | `MODULE_ID_DECLARE(STREAM, 0x29)` (`ERR_MOD_SYS_STREAM`), `ERR_SYS_STREAM_MAP`, the messages |
| `Kconfig` | The number of streams and targets, the queue, the task |
| `host/` | Host tests on fake providers and fake/real session (`HOST_TESTS.MD`) |

## Model

A stream is `sys_stream_cfg_t` `{u8 id, u8 flags}`, set at CREATE and never changed:

- **The id** is the first byte of a PUSH frame's body (`SYS_SESSION.MD` "Push frames"): `0x00` invalid; `0x01`..`0x0F` the board's own streams; `0x10`..`0x7F` user streams; from `0x80` the session's own channels, which no stream takes. CREATE refuses `0x00` and `0x80` or more.
- **The board's own streams**, made by `main` and marked with `sys_stream_mark_system` (REMOVE and SOURCE refuse them): `SYS_STREAM_TELEMETRY` 0x02, `_LOGS` 0x03, `_ERRORS` 0x04 (urgent), `_EVENTS` 0x06 (urgent). This component makes no stream itself.
- **`SYS_STREAM_FLAG_URGENT`**: its frames go to the front of every provider's queue.
- **No `dir` field**: a stream with no `SOURCE` accepts `sys_stream_send()`; a stream that has a `SOURCE` takes its input from that provider's RX hook and refuses `sys_stream_send()` (`ERR_STREAM_DIRECTION`).

**The targets** are one list per stream (`CONFIG_SYS_STREAM_TARGETS`), each of one kind, with one `dropped` count:

| Kind | Made by | What it does |
|---|---|---|
| **SOURCE** | `sys_stream_source(id, provider)`, SOURCE | Opens the local provider's RX hook (`sys_data_provider_open`): what comes in enters the stream queue (unless it has no non-SOURCE target). Refused on system streams. UNBIND or REMOVE closes the provider. |
| **SINK** | `sys_stream_sink(id, board, provider)`, SINK | When `board` is this board (`SYS_SESSION_ID_SELF` or its own id; stored canonically as `SYS_SESSION_ID_SELF`), sends payloads as they are via `sys_data_provider_send(..., NOWAIT)`. Otherwise sends `[DATA][provider][id][payload]` via `sys_session_data()` (max payload `CMD_DATA_MAX - 3`). Configuration: stays when that peer is gone; failures count in `dropped`. |
| **PEER** | `sys_stream_subscribe(id, peer)`, SUBSCRIBE | Sends `[id][payload]` via `sys_session_push()`. The session assigns its PUSH counter per `(peer, class)`. Removed when the peer is gone (`sys_session_on_gone`). |
| **CALLBACK** | `sys_stream_listen(id, fn, ctx)` | Calls `fn(ctx, id, payload, len)` on the stream task. |

**A tunnel between two providers** on two boards is two streams (one on each board), each with one `SOURCE` (its own provider) and one `SINK` (the other board's provider). On one board, a local tunnel uses one stream with a `SOURCE` provider and a local `SINK` `{SYS_SESSION_ID_SELF, provider}`.

## Sending

`sys_stream_send(id, data, len)` **never blocks**: it copies `[id][payload]` into one ring buffer (`CONFIG_SYS_STREAM_QUEUE_SIZE`) and returns. What comes in on a `SOURCE` is queued the same way. The stream task (prio 4, below the router tasks) delivers each message to every target straight from the queue, then frees its room. PUSH sequence counters are owned by `sys_session` per `(peer, class)`, not per stream.

- A stream with a `SOURCE` refuses `sys_stream_send()`: `ERR_STREAM_DIRECTION`. A stream with no target: dropped, `NULL`. Queue full: `ERR_STREAM_QUEUE_FULL` (callers on log/error paths drop silently); on a `SOURCE` RX, full queue or unqueued drop leaves the downstream consumer to notice any gap on its PUSH counter.
- Errors in the stream task are **logged only** (`err_log`): reporting them would queue back into the same task and loop. A target that keeps failing logs once until it succeeds again.

## Remote (`sys_stream_router`, root key `MODULE_ID_STREAM` = 0x29)

Requests are `[0x29][dir][op][args]` (`dir` 0 read, 1 write). Ops and packed layouts live in `sys_stream_router.h`; `contract_stream.json` names them. LIST is a read; the others are writes.

| Op | Request | Response |
|---|---|---|
| `0x01` CREATE | `sys_stream_cfg_t` `{u8 id, u8 flags}` | – |
| `0x02` REMOVE | `sys_stream_ref_t` `{u8 id}` | – |
| `0x03` SOURCE | `sys_stream_source_t` `{u8 id, u8 provider}` | – |
| `0x04` SINK | `sys_stream_sink_t` `{u8 id, u8 board, u8 provider}` | – |
| `0x05` UNBIND | `sys_stream_source_t` `{u8 id, u8 provider}` | – |
| `0x06` UNBIND_ALL | `sys_stream_ref_t` `{u8 id}` | – |
| `0x07` LIST | – | `u8 count`, then per stream `sys_stream_head_t` (`cfg`, `state`, `targets`) and `targets` × `sys_stream_target_t` (`type`, `peer`, `provider`, `dropped`) |
| `0x08` SUBSCRIBE | `sys_stream_ref_t` `{u8 id}` | – (subscriber is `origin->src`) |
| `0x09` UNSUBSCRIBE | `sys_stream_ref_t` `{u8 id}` | – (of `origin->src`) |

## Errors (`ERR_MOD_SYS_STREAM` = `MODULE_ID_STREAM` 0x29)

`ERR_STREAM_NOT_FOUND{stream}`, `_EXISTS{stream}`, `_SYSTEM{stream}`, `_TARGETS_FULL{stream}`, `_TOO_LONG{stream, len, max}`, `_QUEUE_FULL{stream, len}`, `_BAD_ARG`, `_FULL{stream}`, `_DIRECTION{stream}`.
