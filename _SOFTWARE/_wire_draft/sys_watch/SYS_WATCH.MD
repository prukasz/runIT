# sys_watch — watches of reads (draft)

Component `components/sys/sys_watch/` (draft in `_wire_draft/`, not built). A **watch** is a read run on this board by a **trigger** (a period, a change of its answer, an event), its answer sent as an **entry** to the one who added it, its **owner**. The owner sends ADD once and asks for nothing more: this board reads and sends by itself, so a peer that follows a value (a remote device's reads, the app's views) costs the link one entry per change, and no request. The board's own watches send to a connector instead.

The component has two sides: the **watches** this board runs for their owners, and the **client** of the watches this board added on other boards, which hands each entry that comes back to the one who asked for it, and answers this board's reads of other boards from them (Cached reads).

```
 board 2 (runs the watch)                                        board 1 (its owner)
 tick (10 ms), event ─► router_inject(read) ─► work task: read ─► STREAM [WATCHES][entry] ─► client: (src, id) ─► cb
                                                              └─► the board's own watch: its connector
```

## Files

| File | Role |
|---|---|
| `sys_watch.h` | Use: init, add, remove; the client: remote add, remote remove, read, stale |
| `config/sys_watch_config.h` | The tick, the triggers, the target, `sys_watch_head_t`, the entry (published to the app) |
| `router/sys_watch_router.h` | The remote interface: ADD, REMOVE, LIST |
| `errors/` | `ERR_SYS_WATCH_MAP`, its registration, the messages |

## A watch

`sys_watch_head_t` (12 B), then its read (`[key][dir READ][code | id + op][args]`, any read of this board):

| Field | Meaning |
|---|---|
| `id` | 1..255, its owner's: ADD of an id the owner has replaces that watch, so an app that comes back and adds again has one, not two |
| `connector` | where its entries go: `SYS_WATCH_TO_OWNER` (0x00), straight to its owner; or a connector of this board (Entries) |
| `trigger` | `PERIODIC` (every `period`), `ON_CHANGE` (read every `period`, sent when the answer differs from the last entry sent, and after `max_silence` anyway), `ON_EVENT` (when an event passing `filter` happens, at most every `period`) |
| `period`, `max_silence` | in ticks of `SYS_WATCH_TICK_MS` (10 ms): 1 is 10 ms, the most about 11 min |
| `filter` | ON_EVENT: `sys_event_filter_t` (source, event, channel, each exact or its wildcard) |

A request that is no read: `ERR_WATCH_NOT_READ`. Its **owner** is who added it: the `src` of ADD, or `SYS_SESSION_ID_SELF` for the board config's standing watches, which have no peer to send to (`SYS_WATCH_TO_OWNER`: `ERR_WATCH_BAD_ARG`). A watch is known by `(owner, id)`, so the app and the boards that watch this one never take each other's ids.

## Running

A watch has **no task of its own**: its read is a command of the board itself (`router_inject(read, 0, done, watch)`), which runs on the router's work task like any command; `done` runs right after it, on the same task.

- One `esp_timer` every tick (10 ms), on while a watch exists, makes the watches due; an event passing a watch's filter (a queued `sys_event` listener) makes it due at once. Each due watch injects its read; a watch whose read is still queued or running is not injected again. A read the router refuses (its queue full) is an ERROR entry of that error.
- In `done`: ON_CHANGE compares the CRC32 of the answer with that of the last entry that went out (it keeps the CRC, not the answer) and goes on only when it differs or `max_silence` passed; then the entry goes out (Entries).
- `status` is `router_status_e`: OK with the answer, ERROR with the error record. An error goes out once, then again only after the read worked.

## Entries

An entry is `[owner u8][id u8][status u8][data]`, the same to an owner and to a connector.

- **To its owner** (`SYS_WATCH_TO_OWNER`): one STREAM frame of the session per entry, `[SYS_DATA_CONNECTOR_WATCHES][entry]`, sent from `done` with `sys_session_stream` (`NOWAIT` on the first hop; SYS_SESSION.MD "Streams"). Its `seq` is the watch's own count of entries, so a gap is entries of that watch lost. No connector and no subscription: an owner gets the entries of its own watches and nobody else's, through its client (Client) or in the app.
- **To a connector** (any other byte): `sys_data_connector_send(connector, entry)`, and the connector carries it to its targets with its queue (SYS_DATA_CONNECTOR.MD "Sending"). The board's own watches do this (the board config sends a value to an MQTT topic or to the app's view of a connector); a peer may name a connector as well.
- **Not taken** (the first hop busy, down or without a route; a connector's queue full): the entry is not kept, and the watch counts it as not sent: ON_CHANGE sends the value again at its next read, and an error goes again. An entry lost further on is seen only by the owner, as a gap in `seq`; `max_silence` sends the value again within that time.

## Order

A peer's entries leave from the router's work task, right after their read, into the same queue of the same provider as the answers to that peer's commands (`session_answer`, SYS_SESSION.MD "A request for this board"), and the way to the peer keeps the order of its frames. The reads of the watches and the peer's commands run one after another on that task. So **an entry that reaches the owner after the answer to one of its commands was read after that command ran**: once a write is answered, the values that follow are never older than the write. An URGENT command runs and is answered on the urgent task, and a client's NOREPLY one has no answer: neither orders the entries.

## Owners and gone peers

The watches listen for gone peers (`sys_session_on_gone`): every watch whose owner is that peer is removed (the parent's session ending: every owner `sys_session_below()` doesn't hold). An owner that comes back adds its watches again (an ADD of an id it has replaces that watch). The board's own watches (`SYS_SESSION_ID_SELF`) never end.

## Client

What this board asks of another board's watches goes through the client, so no module handles the WATCHES stream itself:

- `sys_watch_remote_add(board, head, read, cb, ctx, &id)`: the client gives the watch an id of its own (1..255, free on that board for this board), sends ADD as a call (SYS_SESSION.MD "The board's own calls") and keeps `(board, id) → cb`. `head.id` is set by the client.
- Each STREAM frame `[WATCHES][entry]` that comes to this board (the session's stream listener, SYS_SESSION.MD "Streams") goes, by its `src` and the entry's `id`, to that `cb` with the entry's status and data, and the gap in the watch's `seq` since the entry before. An entry no one asked for is dropped.
- The ADD answered ERROR, or the board gone (`sys_session_on_gone`): `cb` is told (status ERROR with the error), the watch is kept, and the client sends its ADD again when that board's session opens again. `sys_watch_remote_remove(board, id)` sends REMOVE and forgets it.
- `cb` runs on the RX task the entry came on: it copies what it keeps and returns, never blocks.

Who uses it: the cached reads (below); the board config, for a value of another board it sends on.

## Cached reads

`sys_watch_read(board, req, max_age_ms, wait, buf, cap, &len)` runs the read command `req` (any read, `[key][dir READ][...]`: a device's pin level, a VM status) on another board, and the client decides by itself whether it takes a copy, makes a watch, or asks. **A read is known by its board and its command bytes**: the same command is the same read, whoever makes it (a remote device's driver, the VM, the board config). The caller knows nothing of watches; a remote device's read (SYS_REMOTE.MD "A call") is one.

```
read (board, req, max_age):
  1. a watch of req is ACTIVE, its copy is at most max_age old and not marked old    → the copy
  2. else, wait:    ask now (sys_session_call_wait), answer                           → the answer
          no wait:  ERR_WATCH_NO_VALUE (no copy yet), ERR_WATCH_STALE (older)
  3. no watch of req, and (wait) req was read before within 2 × max_age,
                          (no wait) req is read at all                              → make one:
       ON_CHANGE every max_age / 2 (at least a tick), max_silence max_age: from now on 1.
  4. req not read for 10 × max_age (at least CONFIG_SYS_WATCH_IDLE_MIN_MS)           → REMOVE it
```

A watch costs the link one to two entries per `max_age` (a change, else the silence), however often the value is read; a request costs two frames per read. So a watch pays when the reads come about every `max_age` or more often, and the windows above are in `max_age`, not fixed: with `max_age` 20 ms a driver reading every millisecond gets a watch at its second read, a read every 4 s never does (it asks: half a frame a second, not the watch's 50 to 100); with `max_age` 5 s a read every 4 s does. The removal's floor keeps a watch from coming and going with every pause of a fast reader.

- `max_age_ms` 0 never takes a copy and never makes a watch: every read asks.
- One watch per board and command, however many read it: its period follows the shortest `max_age_ms` it was read with.
- The client full (`CONFIG_SYS_WATCH_CLIENT_MAX`), or the watch's ADD refused there (`ERR_WATCH_FULL` of that board): no watch, the reads ask (or, without wait, answer `ERR_WATCH_NO_VALUE`); the ADD is tried again at the next read after `max_age`.
- **A write makes the copies old.** A copy may have been read there before a write to the same thing. `sys_watch_stale(board, req, target_len)` (`req` the write's command), called when a write's answer came (a remote device's write does): every copy of a read on that board with the same key and the same `target_len` bytes after the dir byte (for the device key the device's id: 2) is marked old, and one plain read of each goes at once, its answer the new copy. That answer, and every entry after it, was read after the write (Order). Until it comes, step 1 skips the copy.


## Remote (`sys_watch_router`, root key `ROUTER_KEY_WATCH` 0x0F)

| Code | Command | Side | Request / answer |
|---|---|---|---|
| `0x01` | ADD | write | `sys_watch_head_t`, then the read; owner: the command's `src` |
| `0x02` | REMOVE | write | `u8 id`, of the command's `src` |
| `0x03` | LIST | read | → `u8 count`, then `sys_watch_info_t` per watch (the head, `u8 owner`, `u8 read_len`) |

## Errors (`ERR_MOD_SYS_WATCH` = 19)

`ERR_WATCH_NOT_FOUND{id}`, `_NOT_READ{id}`, `_BAD_ARG{id}`, `_FULL`; the cached reads' `ERR_WATCH_NO_VALUE{board}` (no copy yet, no wait), `_STALE{board, age_ms}` (the copy older than `max_age_ms`, no wait).

## Limits

| Option | Default | Meaning |
|---|---|---|
| `CONFIG_SYS_WATCH_MAX` | 24 | watches this board runs, of every owner |
| `CONFIG_SYS_WATCH_CLIENT_MAX` | 24 | watches this board added on other boards: the cached reads' and the board config's |
| `CONFIG_SYS_WATCH_IDLE_MIN_MS` | 1000 | a cached read's watch is removed after 10 × its `max_age_ms` without a read, never sooner than this |
| `SYS_WATCH_TICK_MS` | 10 | the unit of a period and the timer's step (`sys_watch_config.h`) |

## Needs elsewhere

- **sys_event**: `sys_event_filter_t` and `sys_event_filter_match` (`_wire_draft/sys_event/sys_event_filter.h`), also for the VM's ON_EVENT block in place of its own fields.
- **router**: `router_inject` with an answer callback (ROUTER.MD).
- **sys_session**: `sys_session_stream` to the owner, `sys_session_call` for the client's ADD and REMOVE, the stream listener, gone peers and the session opening again (SYS_SESSION.MD).
- **sys_data_connector**: `SYS_DATA_CONNECTOR_WATCHES` (0x05) is no connector but the byte of the entries in a STREAM frame; `sys_data_connector_send` for a watch with a connector (SYS_DATA_CONNECTOR.MD).

## Open

- The `.c` files: the table, the timer, the event listener, the entries, the client and its cached reads, the router.
