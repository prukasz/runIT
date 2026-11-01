# sys_watch — watches of reads (for agents)

Component `components/sys/sys_watch/`. A **watch** is a read run on this board by a **trigger** (a period, a change of its answer, an event), its answer sent as an **entry** to the one who added it, its **owner**. The owner sends ADD once and asks for nothing more: this board reads and sends by itself, so a peer that follows a value (the app's views) costs the link one entry per change, and no request. The board's own watches send to a connector instead.

```
 this board (runs the watch)                                          its owner
 tick (10 ms), event ─► router_inject(read) ─► work task: read ─► STREAM [WATCHES][entry] ─► the owner
                                                              └─► the board's own watch: a connector
```

## Files

| File | Role |
|---|---|
| `sys_watch.h` | Use: init, add, remove, list |
| `sys_watch.c` | The table, the timer, the event subscriptions, the entries |
| `config/sys_watch_config.h` | The tick, the triggers, the target, `sys_watch_head_t`, `sys_watch_info_t` (published to the app) |
| `router/sys_watch_router.h` / `.c` | The remote interface: ADD, REMOVE, LIST (root key `ROUTER_KEY_WATCH` 0x0F) |
| `router/contract_watch.json` | The router's descriptor |
| `errors/` | `ERR_SYS_WATCH_MAP`, its registration, the messages |
| `Kconfig` | The number of watches |
| `AGENTS.md` | Rules for changing this folder |

## A watch

`sys_watch_head_t` (12 B), then its read (`[key][dir READ][code | id + op][args]`, any read of this board, at most `SYS_WATCH_READ_MAX` bytes):

| Field | Meaning |
|---|---|
| `id` | 1..255, its owner's: ADD of an id the owner has replaces that watch, so an app that comes back and adds again has one, not two |
| `connector` | where its entries go: `SYS_WATCH_TO_OWNER` (0x00), straight to its owner; or a connector of this board (Entries) |
| `trigger` | `PERIODIC` (every `period`), `ON_CHANGE` (read every `period`, sent when the answer differs from the last entry sent, and after `max_silence` anyway), `ON_EVENT` (when an event passing `filter` happens, at most every `period`) |
| `period`, `max_silence` | in ticks of `SYS_WATCH_TICK_MS` (10 ms): 1 is 10 ms, the most about 11 min; `period` 0 is refused |
| `filter` | ON_EVENT: `sys_event_filter_t` (source, event, channel, each exact or its wildcard; SYS_EVENT.MD) |

A request that is no read: `ERR_WATCH_NOT_READ`; a bad id, period, trigger or target, or a read that is too long: `ERR_WATCH_BAD_ARG`; every slot taken: `ERR_WATCH_FULL`. Its **owner** is who added it: the `src` of ADD, or `SYS_SESSION_ID_SELF` for the board config's standing watches, which have no peer to send to (`SYS_WATCH_TO_OWNER` with it: `ERR_WATCH_BAD_ARG`). A watch is known by `(owner, id)`, so the app and the boards that watch this one never take each other's ids.

## Running

A watch has **no task of its own**: its read is a command of the board itself (`router_inject(read, 0, done, watch)`), which runs on the router's work task like any command; `done` runs right after it, on the same task.

- One `esp_timer` every tick (10 ms), on while a watch exists, makes the watches due. An ON_EVENT watch subscribes its filter with `sys_event_subscribe_fn` when added (and drops it when it ends); a matching event marks it, and it runs at once when its `period` since the last read has passed, else at the tick that completes it. Each due watch injects its read; a watch whose read is still queued or running is not injected again. The first read of a new watch is at the next tick. A read the router refuses (its queue full) is an ERROR entry of that error.
- In `done`: ON_CHANGE compares the CRC32 of the answer with that of the last entry that went out (it keeps the CRC, not the answer) and goes on only when it differs or `max_silence` passed; then the entry goes out (Entries).
- `status` is `router_status_e`: OK with the answer, ERROR with the error record. An error goes out once, then again only after the read worked.
- ADD of an id the owner has stops the old watch (an answer of the old one that is still in flight is dropped) and starts the new one from scratch.

## Entries

An entry is `[owner u8][id u8][status u8][data]`, the same to an owner and to a connector. The answer's buffer has `CMD_HEAD` bytes in front of its data, so the entry is made in place, without a copy.

- **To its owner** (`SYS_WATCH_TO_OWNER`): one STREAM frame of the session per entry, `[SYS_DATA_CONNECTOR_WATCHES][entry]`, sent from `done` with `sys_session_stream` (`NOWAIT`; SYS_SESSION.MD "Streams"). Its `seq` is the watch's own count of entries, so a gap is entries of that watch lost. No connector and no subscription: an owner gets the entries of its own watches and nobody else's.
- **To a connector** (any other byte): `sys_data_connector_send(connector, entry)`, and the connector carries it to its targets with its queue (SYS_DATA_CONNECTOR.MD "Sending"). The board's own watches do this; a peer may name a connector as well.
- **Not taken** (the way busy, down or without a route; a connector's queue full; an entry longer than a frame): the entry is not kept, and the watch counts it as not sent: ON_CHANGE sends the value again at its next read, and an error goes again. An entry lost further on is seen only by the owner, as a gap in `seq`; `max_silence` sends the value again within that time.

## Order

A peer's entries leave from the router's work task, right after their read, into the same queue of the same provider as the answers to that peer's commands (`session_answer`, SYS_SESSION.MD "A request for this board"), and the way to the peer keeps the order of its frames. The reads of the watches and the peer's commands run one after another on that task. So **an entry that reaches the owner after the answer to one of its commands was read after that command ran**: once a write is answered, the values that follow are never older than the write. An URGENT command runs and is answered on the urgent task, and a client's NOREPLY one has no answer: neither orders the entries.

## Owners and gone peers

The watches listen for gone peers (`sys_session_on_gone`): every watch whose owner is that peer is removed. An owner that comes back adds its watches again (an ADD of an id it has replaces that watch). The board's own watches (`SYS_SESSION_ID_SELF`) never end.

## Remote (`sys_watch_router`, root key `ROUTER_KEY_WATCH` 0x0F)

| Code | Command | Side | Request / answer |
|---|---|---|---|
| `0x01` | ADD | write | `sys_watch_head_t`, then the read; owner: the command's `src` |
| `0x02` | REMOVE | write | `u8 id`, of the command's `src` |
| `0x03` | LIST | read | → `u8 count`, then `sys_watch_info_t` per watch (the head, `u8 owner`, `u8 read_len`) |

## Errors (`ERR_MOD_SYS_WATCH` = 19)

`ERR_WATCH_NOT_FOUND{id}`, `_NOT_READ{id}`, `_BAD_ARG{id}`, `_FULL`.

## Limits

| Option | Default | Meaning |
|---|---|---|
| `CONFIG_SYS_WATCH_MAX` | 24 | watches this board runs, of every owner |
| `SYS_WATCH_TICK_MS` | 10 | the unit of a period and the timer's step (`sys_watch_config.h`) |
| `SYS_WATCH_READ_MAX` | 48 | the longest read a watch keeps (`sys_watch_config.h`) |

Each ON_EVENT watch takes one of `CONFIG_SYS_EVENT_MAX_SUBS` (SYS_EVENT.MD).

## Needs elsewhere

- **sys_event**: `sys_event_filter_t` and `sys_event_filter_match`, `sys_event_subscribe_fn`.
- **router**: `router_inject` with an answer callback, `router_origin` (ROUTER.MD).
- **sys_session**: `sys_session_stream` to the owner, `sys_session_on_gone` (SYS_SESSION.MD).
- **sys_data_connector**: `SYS_DATA_CONNECTOR_WATCHES` (0x05) is no connector but the byte of the entries in a STREAM frame; `sys_data_connector_send` for a watch with a connector (SYS_DATA_CONNECTOR.MD).

## Open

- Other boards' watches (the client that adds them and hands each entry to the one who asked) and the cached reads on top of them: the draft in `_wire_draft/sys_watch/`.
- The board config adding standing watches of its own (`SYS_SESSION_ID_SELF`).
