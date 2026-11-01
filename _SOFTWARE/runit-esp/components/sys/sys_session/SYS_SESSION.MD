# sys_session — sessions

Component `components/sys/sys_session/`. A **session** is a framed provider (SYS_DATA_PROVIDER.MD) attached to a **peer**: a client of this board (the app, a test host). The session layer owns the frames: it hands the commands for this board to the router (ROUTER.MD) and frames their answers, carries the streams to the peers subscribed to them, opens and ends sessions, and tells who is gone. It has **no task and no queue** of its own: what comes in is handled on the driver's RX task, and what goes out waits only in a driver's queue. Code knows a peer by its **id**: the client id this board gave it.

```
 provider (driver RX task)              session layer                                     router
 ─────────────────────────              ─────────────                                     ──────
 frame ── rx ──► for this board? ─ request: checks, err_cmd_t ── router_submit ───────►  rings, tasks, handlers
                 │                                                                            │
                 │                 answer frame ◄── session_answer (the command's done) ◄─────┘
                 ├ a stream for me ──────► the stream listeners
                 └ control: hello, ping, no session
 one esp_timer (10 ms): pings, session timeouts
```

The board is the **root**: it takes clients only. A board's parent and children (a tree of boards over UART, forwarding, calls to another board) are not built: `sys_session_attach` refuses PARENT and CHILD.

## Files

| File | Role |
|---|---|
| `sys_session.h` | Use: init, board id, attach, detach, timeout, call (this board), wired, stream, below, gone, reached and stream listeners |
| `sys_session.c` | The sessions, the frames in and out, the timer |
| `config/sys_session_config.h` | The version, the ids, the frame byte, kinds, control ops, peer kinds, access (published to the app) |
| `frame/sys_session_frame.h` | The header and the hello |
| `internal/sys_session_internal.h` | For its router only: PEERS |
| `router/sys_session_router.h` / `.c` | The remote interface: ID, ATTACH, DETACH, PEERS |
| `router/contract_session.json` | The router's descriptor |
| `errors/` | `ERR_SYS_SESSION_MAP`, its registration, the messages |
| `Kconfig` | Sessions, listeners, the session timeout |

## Ids

A **board id** (0x00..0xEF) is a board's place in the project, set by the app (ID, kept in NVS, used from the next start), not its identity: its identity is the MAC in its hello. A new board has `SYS_SESSION_ID_DEFAULT` (0x00). **Clients** are not boards: the board gives each one an id (0xF0..0xFC) when its session opens, one per client session, and checks that every frame from that session carries it. `SYS_SESSION_ID_SELF` (0xFE) is the board itself as the source of its own commands (`ROUTER_SRC_SELF`): it is in error records, never in a frame's header. `SYS_SESSION_ID_NONE` (0xFD) is a client before it has an id; `SYS_SESSION_ID_BROADCAST` (0xFF) every board.

## Frames

One header on every frame (`sys_session_hdr_t`, 5 B = `CMD_HEAD`): the frame byte, `dst`, `src`, `seq u16`.

| Frame byte | Meaning |
|---|---|
| bits 0-1 | a request's or a stream's flags: `ROUTER_FLAG_URGENT`, `_NOREPLY` (the router's own) |
| bit 2 | an answer's status: `router_status_e` (OK, ERROR) |
| bits 6-7 | the kind |

| Kind | After the header | `seq` |
|---|---|---|
| `REQUEST` | `[key][dir][code \| id + op][args]` | the source's counter |
| `ANSWER` | the data; ERROR's is the error record (ERRORS.MD "Error record") | the request's |
| `STREAM` | `[connector][payload]`: one connector frame, to a subscriber | the connector's frame counter: a gap is frames lost |
| `CONTROL` | `op`, then its data: HELLO (`sys_session_hello_t`), PING, NO_SESSION | 0 |

What follows the header is at most `CMD_DATA_MAX`. The frame size is the protocol's (CMD.MD "Frame size"): every peer of one `SYS_SESSION_VERSION` takes `CMD_FRAME_MAX`.

Example: a client 0xF0 reads the uptime (sys PING, urgent), seq 7: `01 00 f0 07 00 | 01 00 01`; the answer `40 f0 00 07 00 | e8 03 00 00`.

## A session's life

1. `sys_session_attach(provider, peer, access)`: the provider must be framed (`ERR_SESSION_NOT_FRAMED`); the session opens it (its one user). The board config attaches the UART console and the runIT BLE pair as clients with full access (`runit.c`).
2. When the provider comes up (its rx with `max_frame` > 0; the UART at once), the board sends its HELLO (version, id, MAC, `boot_id`, its timeout of this session). A BLE client not subscribed yet misses it: that is fine, its own hello asks again.
3. A client sends its HELLO with id `NONE`; the board gives it the first free client id, opens the session and answers with its HELLO, whose `your_id` is that id. The board answers every hello of a client, so one that missed the answer sends its hello again. Another version: `ERR_SESSION_VERSION`, the session stays closed. No free client id: `ERR_SESSION_FULL`.
4. Until the session opens, only control frames pass: anything else is answered NO_SESSION and the board's HELLO (`ERR_SESSION_NONE` reported), so a peer that missed a restart (UART has no disconnect) starts over. A frame whose `src` is not the session's client id is treated the same.
5. A hello on an open session updates it (a new timeout); a new `boot_id` ends it first: the peer restarted, so what it had here is gone. A NO_SESSION from the peer ends the session and the board says hello again.
6. **Timeouts.** The board ends a session after its own timeout of silence and says it in its hello; the peer sends something at least every third of it, a PING when it has nothing else. The board does the same towards the peer (by the peer's hello's timeout). A client's session takes `CONFIG_SYS_SESSION_TIMEOUT_MS` (3 s), or the dead-man's time while it is armed (`sys_session_set_timeout`, which sends a hello on every open session). Nothing received for the timeout, or the provider down: the session **ends**: its peer is gone (Gone peers), the hello starts again when frames come.

One `esp_timer` (every 10 ms) does it all: the pings and the session timeouts. A session's timeout is 150..60000 ms (`SYS_SESSION_TIMEOUT_MIN_MS`, `_MAX_MS`).

## A request for this board

On the driver's RX task, in order: the session is open, from its client (else NO_SESSION); `dst` is this board or BROADCAST (else `ERR_SESSION_NO_ROUTE`: no boards are reached through this one); a write from a read-only peer is refused (`ERR_SESSION_DENIED`); then the command goes to the router, with the session's framer as the callback of its answer:

```c
err_cmd_t cmd = {.req = body, .req_len = len, .seq = hdr.seq, .src = hdr.src,
                 .flags = hdr.frame & SYS_SESSION_FRAME_FLAGS};
err_h err = router_submit(&cmd, session_answer, NULL);   // copied; never blocks
```

- A BROADCAST runs with NOREPLY: it is never answered.
- Refused (no route, denied, `ERR_ROUTER_QUEUE_FULL`, `_FRAME_LEN`): the session answers ERROR to its source at once (`NOWAIT`; not for NOREPLY) and reports it with the command (`router_report_cmd`).
- `session_answer` (the router's `done`, on the router task): finds the open session of `cmd->src`, writes the header in the room in front of the data (status, `seq`) and sends it on that provider, waiting for room at most the driver's bound (no `NOWAIT`: an answer is not dropped lightly; URGENT for an urgent command). No session for it: `ERR_SESSION_NO_ROUTE`, the router's to report.
- An error record names the command's `src` and `seq`, so a source finds its own errors on the errors stream too.
- A source retries a read after its timeout; a write without an answer is read back, never sent twice by the session's doing. `seq` is the source's `u16` counter.

## The board's own calls

`sys_session_call(dst, req, flags, cb, ctx)` runs a command on this board (its id or `SYS_SESSION_ID_SELF`) and gives its answer to `cb`, a `router_done_f`: `router_inject(req, flags, cb, ctx)`, the answer on the router task. Another board: `ERR_SESSION_NO_ROUTE`.

## Streams

A peer subscribes to a connector of this board with SUBSCRIBE (SYS_DATA_CONNECTOR.MD "Model"); the connector task then sends each frame of it with `sys_session_stream(peer, seq, data, len, flags)`: one STREAM frame (`[connector][payload]`, `seq` the connector's frame counter), `NOWAIT` (and `URGENT` when the connector is), so a busy provider is `ERR_PROV_BUSY` at once.

A STREAM frame for this board goes to every stream listener (`sys_session_on_stream`, `CONFIG_SYS_SESSION_STREAM_LISTENERS`), each taking its own by the first byte. Clients send none today. A peer whose session opened is told to every `sys_session_on_reached` listener.

## Gone peers (and the dead-man)

A session of this board that ends (silence past its timeout, the provider down, a new `boot_id`, NO_SESSION, detach) makes its peer **gone**. Every listener is told (`sys_session_on_gone(fn(peer, via))`, `via` the session's `sys_session_peer_e`) on the context that noticed it (the driver's RX task, the session's timer, the caller of `sys_session_detach`); it drops what it keeps for that peer and never blocks:

| Listener | Does |
|---|---|
| the connectors | remove its subscriptions (SYS_DATA_CONNECTOR.MD "Model") |
| the dead-man (`sys_system`) | `via` CLIENT, when armed: the safe state |

**The dead-man** needs no timer of its own: the silence of a client's session is it. sys_system's DEADMAN arms it (a timeout of 0 disarms it) and sets its time, which is the clients' sessions' timeout (`sys_session_set_timeout`: the peer, told by the new hello, sends at least every third of it). A session ends whether the dead-man is armed or not; only an armed one enters the safe state, and BYE disarms it before a client leaves. A BLE client that drops ends its session at once (its provider goes down): the lost link.

## Remote (`sys_session_router`, root key `ROUTER_KEY_SESSION` 0x0E)

| Code | Command | Side | Request / answer |
|---|---|---|---|
| `0x01` | ID | read / write | write `u8 board_id` (NVS, from the next start); read → `u8 board_id` |
| `0x02` | ATTACH | write | provider id, `u8 peer`, `u8 access` |
| `0x03` | DETACH | write | provider id |
| `0x04` | PEERS | read | → `u8 count`, then `sys_session_info_t` per session (provider, peer, access, open, peer id, this board's timeout of it, subtree size: 0) |

## Errors (`ERR_MOD_SYS_SESSION` = 18)

The map (`errors/sys_session_errors.h`) holds the tags of the whole protocol; this board raises `ERR_SESSION_VERSION{got, mine}`, `_NONE{src}`, `_NO_ROUTE{dst}`, `_DENIED{src}`, `_NOT_FRAMED{link, channel}`, `_CLIENT` (attach of a PARENT or CHILD), `_FULL` (a session, a client id or a listener slot). The others (`_ID_TAKEN`, `_TIMEOUT`, `_PEER_DOWN`, `_PARENT_TAKEN`, `_CALLS_FULL`, `ERR_REMOTE`, `_WOULD_BLOCK`, `_BUSY`) belong to the calls between boards. A busy, down or too short provider is its own `ERR_PROV_*`.

## Limits

| Option | Default | Meaning |
|---|---|---|
| `CONFIG_SYS_SESSION_MAX` | 4 | sessions: the board's clients |
| `CONFIG_SYS_SESSION_GONE_LISTENERS` | 5 | `sys_session_on_gone` and `sys_session_on_reached` slots, each |
| `CONFIG_SYS_SESSION_STREAM_LISTENERS` | 2 | `sys_session_on_stream` slots |
| `CONFIG_SYS_SESSION_TIMEOUT_MS` | 3000 | a client's session ends after this much silence while the dead-man is off (150..60000) |

## Tests

The host scripts (`router_link.py`, `.claude/skills/runit-esp/scripts/`) are a client: HELLO, PING, NO_SESSION, a new session after a restart, the streams subscribed. `router_tests.py` covers PEERS, ID, ATTACH refused, a request for another board, the dead-man by the session's silence; the app's backend (`runit-app/src/backend/session/link.ts`) is the BLE client, tested by `npm run test:hw`.
