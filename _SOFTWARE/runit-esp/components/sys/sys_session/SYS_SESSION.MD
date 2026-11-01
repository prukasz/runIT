# sys_session — sessions

Component `components/sys/sys_session/`. A **session** is a framed provider (SYS_DATA_PROVIDER.MD) attached to a **peer**: the board's parent, a child, or (on the root) a client. The session layer owns the frames: it hands the commands for this board to the router (ROUTER.MD), forwards the frames for other boards without the router, carries the streams to the peers subscribed to them, makes the board's own calls and tells who is gone. It has **no task and no queue** of its own: what comes in is handled on the driver's RX task, and what goes out waits only in a driver's queue. Code knows a peer by its **id**: a board id, or a client id the root gave.

```
 provider (driver RX task)              session layer                                     router
 ─────────────────────────              ─────────────                                     ──────
 frame ── rx ──► for this board? ─ request: checks, err_cmd_t ── router_submit ───────►  rings, tasks, handlers
                 │                                                                            │
                 │                 answer frame ◄── session_answer (the command's done) ◄─────┘
                 ├ an answer to my call ─► the call's callback
                 ├ a stream for me ──────► the stream listeners
                 ├ for another board ────► the next hop's provider, NOWAIT
                 └ control: hello, ping, no session, gone
 one esp_timer (10 ms): pings, session timeouts, call timeouts
```

## Files

| File | Role |
|---|---|
| `sys_session.h` | Use: init, board id, root, attach, detach, timeout, call (here or there), call and wait, wired, stream, below, gone, reached and stream listeners |
| `sys_session.c` | The sessions, the route, the frames in and out, the calls, the timer |
| `config/sys_session_config.h` | The version, the ids, the frame byte, kinds, control ops, peer kinds, access (published to the app) |
| `frame/sys_session_frame.h` | The header and the hello |
| `internal/sys_session_internal.h` | For its router only: PEERS |
| `router/sys_session_router.h` / `.c` | The remote interface: ID, ATTACH, DETACH, PEERS |
| `router/contract_session.json` | The router's descriptor |
| `errors/` | `ERR_SYS_SESSION_MAP`, its registration, the messages |
| `Kconfig` | Sessions, calls, listeners, timeouts |

## Boards, clients, the tree

A **board id** (0x00..0xEF) is a board's place in the project, set by the app (ID, kept in NVS), not its identity: a board swapped for another of its type takes the same id and the project runs on; its identity is the MAC in its hello. A new board has `SYS_SESSION_ID_DEFAULT` (0x00).

The boards are a **tree**: each has at most one parent and any number of children. The **root** is the board without a parent, so a board alone is one; it is the board the app talks to, 0x00 by convention. **Clients** (the app, a test host) are not boards: only a board without a parent takes them, and it gives each one an id (0xF0..0xFC) when its session opens, one per client session, and checks that every frame from that session carries it. `SYS_SESSION_ID_SELF` (0xFE) is the board itself as the source of its own commands (`ROUTER_SRC_SELF`): it is in error records, never in a frame's header.

A board's hello to its parent lists its **subtree** (the boards reached through it); it sends its hello again when that changes, so every board knows, per child, which ids lie behind it.

| From | dst | Goes |
|---|---|---|
| anywhere | this board | to the router (a request), to my call (an answer), to the stream listeners (a stream), to the session (control) |
| a child or a client | in the subtree of a child | down to that child |
| a child or a client | elsewhere | up to the parent; on the root: to the client of that id, else `ERR_SESSION_NO_ROUTE` |
| the parent | in the subtree of a child | down to that child |
| the parent | elsewhere | dropped, `ERR_SESSION_NO_ROUTE` to the source |
| anywhere | `0xFF` broadcast | up to the root, which runs it and sends it down to every child; each board runs it on the way down; never answered. A broadcast the root itself makes (`sys_session_call`) is only sent down: the root has done what it broadcasts |

A frame so goes up, then down, and never loops.

## Frames

One header on every frame (`sys_session_hdr_t`, 5 B = `CMD_HEAD`): the frame byte, `dst`, `src`, `seq u16`. A board forwards a frame by its `dst` without reading or changing the rest.

| Frame byte | Meaning |
|---|---|
| bits 0-1 | a request's or a stream's flags: `ROUTER_FLAG_URGENT`, `_NOREPLY` (the router's own) |
| bit 2 | an answer's status: `router_status_e` (OK, ERROR) |
| bits 6-7 | the kind |

| Kind | After the header | `seq` |
|---|---|---|
| `REQUEST` | `[key][dir][code \| id + op][args]` | the source's counter |
| `ANSWER` | the data; ERROR's is the error record | the request's |
| `STREAM` | `[connector][payload]`: one connector frame, to a subscriber; or `[WATCHES][entry]`, a watch's entry to its owner (Streams) | the connector's frame counter, or the watch's count of entries: a gap is frames lost |
| `CONTROL` | `op`, then its data: HELLO (`sys_session_hello_t`), PING, NO_SESSION between two neighbours (`dst`, `src` their ids); GONE from the parent (Gone peers) | 0 |

What follows the header is at most `CMD_DATA_MAX`. The frame size is the protocol's (CMD.MD "Frame size"): every peer of one `SYS_SESSION_VERSION` takes `CMD_FRAME_MAX`, so nothing is sized per hop.

## A session's life

1. `sys_session_attach(provider, peer, access)`: the provider must be framed (`ERR_SESSION_NOT_FRAMED`); the session opens it. One parent at most (`ERR_SESSION_PARENT_TAKEN`); a client only on a board without a parent, and no parent on a board with clients (`ERR_SESSION_CLIENT`).
2. When the provider comes up (its rx with `max_frame` > 0), the board sends HELLO (version, id, MAC, `boot_id`, descriptors' hash, board type, firmware, its timeout, to its parent its subtree). A client sends its hello with id `NONE`; the root's answer gives it `your_id`.
3. The session **opens** when both hellos have passed: the same version (`ERR_SESSION_VERSION`, else it stays closed), an id no other session has (`ERR_SESSION_ID_TAKEN`).
4. Until then only control frames pass: anything else is answered `NO_SESSION` with the hello, so a peer that missed a restart (UART has no disconnect) starts over.
5. A hello on an open session updates it (a new subtree, a new timeout); a new `boot_id` ends it first: the peer restarted, so what it had here is gone.
6. **Timeouts.** Each side ends the session after its own timeout of silence and says it in its hello; so each side sends something at least every third of the other's timeout, a PING when it has nothing else. A child's session takes `CONFIG_SYS_SESSION_TIMEOUT_MS` (3 s); the parent's and the clients' take the dead-man's time (`sys_session_set_timeout`). Nothing received for the timeout, or the provider down: the session **ends**: the calls through it fail (`ERR_SESSION_PEER_DOWN`), its peer and the boards behind it are gone (Gone peers), the hello starts again when frames come.

One `esp_timer` (every 10 ms) does it all: the pings, the session timeouts, the calls' timeouts. A session's timeout is at least 150 ms, a call's 10 ms.

## A request for this board

On the driver's RX task, in order: the session is open (else NO_SESSION); a write from a read-only peer is refused (`ERR_SESSION_DENIED`); then the command goes to the router, with the session's framer as the callback of its answer:

```c
err_cmd_t cmd = {.req = body, .req_len = len, .seq = hdr.seq, .src = hdr.src,
                 .flags = hdr.frame & SYS_SESSION_FRAME_FLAGS};
err_h err = router_submit(&cmd, session_answer, NULL);   // copied; never blocks
```

- The router refused it (`ERR_ROUTER_QUEUE_FULL`, `_FRAME_LEN`): the session answers ERROR to its source at once (`NOWAIT`; not for NOREPLY) and reports it with the command.
- `session_answer` (the router's `done`, on the router task): finds the session towards `cmd->src`, writes the answer frame (the header with its status, the data) and sends it on that provider, waiting for room at most the driver's bound (no `NOWAIT`: an answer is not dropped lightly). A failure is the router's to report.
- An error record names the command's `src` and `seq` (ERRORS.MD "Error record"), so a source finds its own errors on the errors stream too.

## Forwarding

A frame for another board is sent on from the driver's RX task it came in on: to the next hop's provider, with `NOWAIT` (and `URGENT` when its flag says so). The driver's queue is the only queue it waits in; the router never sees it, and the board keeps no state of it (its answer comes back by the same rules, to `src`).

- A request that can't go on is answered to its source (unless NOREPLY) with why: the next hop's own `ERR_PROV_BUSY`, `_DOWN` or `_TOO_LONG`, or `ERR_SESSION_NO_ROUTE`.
- An answer or a stream frame that can't go on is dropped (a reader times out and retries; a stream shows a gap in its seq).

## The board's own calls

`sys_session_call(dst, req, flags, timeout_ms, cb, ctx)` runs a command and gives its answer to `cb`, a `router_done_f`: the callback the router calls for a command it ran, so a module that needs a command's answer gets it the same way here or on another board.

- **To this board** (its id or `SYS_SESSION_ID_SELF`): `router_inject(req, flags, cb, ctx)`: no slot, no timeout, the answer on the router task.
- **To another board**: the request goes out (`src` this board, `seq` its counter, `NOWAIT` on the first hop, so a busy way is `ERR_PROV_BUSY` at once) and a call slot (`CONFIG_SYS_SESSION_CALLS`, 32) keeps it until the answer:

| Answer | `cb` gets |
|---|---|
| OK | the data; the call ends |
| ERROR | that board's error record with `ERR_REMOTE{board, seq}` put before its chain, so `err_record_parse` gives this board's chain to report and the error policy's walk stops at it; the call ends |
| none in its timeout | ERROR: this board's record of `ERR_SESSION_TIMEOUT{dst, seq}` |
| the session it went through ends | ERROR: this board's record of `ERR_SESSION_PEER_DOWN{board}` |

An answer comes on the RX task it arrived on, the last two on the session's timer: `cb` copies what it keeps and returns, and may call what never blocks (another call, a connector's send). A call ends once, under the session's lock: a late answer finds no call and is dropped. A slot keeps the request's first `CONFIG_ERRORS_RECORD_REQ_MAX` bytes, for the `cmd` it gives `cb` and the record of an error made here. Who calls: sys_system (the root's broadcasts); later the remote devices' writes, the watches' client, the mirrors, the board config, the flasher.

### Answers

**Every call to another board is answered**: OK when it ran, ERROR with why. So no failure between boards goes unseen: a call ends with its answer, or with this board's error when none came. `NOREPLY` goes between boards on a broadcast only (STOP, the time), which no one answers; a call to one board with `NOREPLY` is `ERR_CMD_BAD_ARG`. A client's request may carry it: the board runs it without an answer, and a failure goes to the errors stream.

An answer costs a frame: on a cable next to nothing (an OK is the 5-byte header), on a radio a packet. **At most `CONFIG_SYS_SESSION_PEER_IN_FLIGHT` calls to one board are on the way at once**; one more is `ERR_SESSION_BUSY`, which is no failure but "later". A caller that sends often sends again when the answers have come back, never faster.

**Timeouts follow the way.** `timeout_ms` 0 takes the way's own: `CONFIG_SYS_SESSION_CALL_TIMEOUT_WIRED_MS` (50 ms) to the peer of a `WIRED` session (`sys_session_wired`), else `CONFIG_SYS_SESSION_CALL_TIMEOUT_MS` (1 s), longer than a radio's tail (a few hundred ms over MQTT), so a slow answer is not taken for a lost one. The session's timer checks them every 10 ms.

### Waiting for the answer

`sys_session_call_wait(dst, req, flags, timeout_ms, buf, cap, &len)` is the same call, waited for: the caller's task sleeps until the call ends (a task notification from the RX task or the timer that ended it) and gets the answer in `buf`, or the error as a chain: `ERR_REMOTE{board, seq}` over that board's chain, or this board's `ERR_SESSION_TIMEOUT` / `_PEER_DOWN`. An answer longer than `cap`: `ERR_CMD_BAD_LEN`. Waiting is for:

| Who | Way |
|---|---|
| configuration: the board config, the flasher, a start sequence | any: a long wait does not matter there |
| a remote device in `blocking` mode: its call, on whatever task makes it | a `WIRED` neighbour only: the caller waits a millisecond, not a radio's round trip |

It refuses to wait (`ERR_SESSION_WOULD_BLOCK`) for a call to this board on a router task (`router_is_task`: it would wait for itself), and for any call on a task marked not to wait: a driver's RX task (the session marks each by the frames it delivers), the session's timer (the `esp_timer` task, which the watches' and the clock's timers share) and whoever called `sys_session_no_wait()` (the connector task). A router task may wait for another board: the commands behind it wait that long too, at most the wired timeout, as behind a slow I2C transfer. Two boards that wait for each other (a blocking remote device each way) end in their timeouts, never hang; the app does not build such a cycle.

`sys_session_wired(peer)`: whether `peer` is the peer of an open session whose provider is `WIRED` (`sys_data_provider_state`): a neighbour on a cable. The `blocking` mode and the wired timeout ask it.

## Streams

A peer subscribes to a connector of this board with SUBSCRIBE (SYS_DATA_CONNECTOR.MD "Subscribers"); the connector task then sends each frame of it with `sys_session_stream(peer, seq, data, len, flags)`: one STREAM frame (`[connector][payload]`, `seq` the connector's frame counter), routed as any frame, `NOWAIT` on the first hop.

A watch of this board sends its entries the same way, straight to its owner, with no subscription: `[SYS_DATA_CONNECTOR_WATCHES][entry]`, `seq` the watch's count of entries (SYS_WATCH.MD "Entries"). It sends from the router's work task, as `session_answer` does, so towards one peer its entries and the answers to that peer's commands leave in the order they were made (SYS_WATCH.MD "Order").

A STREAM frame for this board goes to every stream listener (`sys_session_on_stream`, `CONFIG_SYS_SESSION_STREAM_LISTENERS`), and each takes its own by the first byte. So no module reads another board's streams by itself, and any number of them get the same stream. No module listens yet.

A peer that can be reached (a session opened, a child's hello names it) is told to every listener of `sys_session_on_reached(fn(peer))`: sys_system sends the root's time to the boards.

## Retries

A source retries a request after its timeout only when it is a read (a read changes nothing). A write without an answer is reported to whoever asked (`ERR_SESSION_TIMEOUT`), who reads the state back to know whether it ran; a caller that keeps a setting sends it again by itself. So no board keeps what it ran, and a write never runs twice by the session's doing. `seq` is the source's `u16` counter: with timeouts of seconds it never comes round on a live request.

## Gone peers (and the dead-man)

A peer that can no longer be reached is **gone**, and every board learns it:

- A session of this board ends (silence past its timeout, the provider down, a new `boot_id`, detach): its peer and every board behind it are gone here. The parent's session ending takes every peer outside this board's subtree.
- A child's hello lists a smaller subtree: the boards no longer in it are gone here, and this board's own hello takes the change up, so every board above learns it the same way.
- The root, which every loss reaches (by the hellos; the clients are its own), sends **GONE** (a control frame, `[count][ids]`) to every child, and each board passes it on to its children: so a board learns of a peer it reaches through its parent (another branch's board, a client).

Every listener is told (`sys_session_on_gone(fn(peer, via))`) on the context that noticed it; it drops what it keeps for that peer and never blocks:

| Listener | Does |
|---|---|
| the connectors | remove its subscriptions (SYS_DATA_CONNECTOR.MD "Subscribers") |
| the watches (SYS_WATCH.MD) | remove the watches it owns |
| the dead-man (`sys_system`) | `via` PARENT or CLIENT, when armed: the safe state |

The parent's session ending is told once, with `peer` `SYS_SESSION_ID_BROADCAST` and `via` PARENT: a listener drops every peer of its own that `sys_session_below()` doesn't hold.

**The dead-man** needs no timer of its own: the silence of the parent's or a client's session is it. sys_system's DEADMAN arms it (a timeout of 0 disarms it) and sets its time, which is those sessions' timeout (`sys_session_set_timeout`: the peer, told by the new hello, sends at least every third of it). A session ends whether the dead-man is armed or not, since its peers are gone either way; only an armed one enters the safe state, and BYE disarms it before a client leaves. **The root's** dead-man also sends STOP to every board (`SYS_SESSION_ID_BROADCAST`, URGENT and NOREPLY; `sys_system`): a child's own dead-man follows its parent's session, which stays up when only the app is gone.

**The time** goes the same way: the time a client sets on the root (sys_system's TIME) the root sends to every board as a broadcast, when a board is reached and again every `CONFIG_SYS_SESSION_TIME_PERIOD_S` (`sys_system_broadcast_time`), so every board's clock is the root's and an event's `t_ms` (the clock of the board where it happened) compares across boards.

## Remote (`sys_session_router`, root key `ROUTER_KEY_SESSION` 0x0E)

| Code | Command | Side | Request / answer |
|---|---|---|---|
| `0x01` | ID | read / write | write `u8 board_id` (NVS, from the next start); read → `u8 board_id` |
| `0x02` | ATTACH | write | provider id, `u8 peer`, `u8 access` |
| `0x03` | DETACH | write | provider id |
| `0x04` | PEERS | read | → `u8 count`, then `sys_session_info_t` per session (provider, peer, access, open, peer id, this board's timeout of it, subtree size) |

## Limits

| Option | Default | Meaning |
|---|---|---|
| `CONFIG_SYS_SESSION_MAX` | 4 | sessions: a parent, children, the root's clients |
| `CONFIG_SYS_SESSION_CALLS` | 32 | the board's calls to other boards in flight |
| `CONFIG_SYS_SESSION_PEER_IN_FLIGHT` | 4 | of them to one board; one more: `ERR_SESSION_BUSY` |
| `CONFIG_SYS_SESSION_CALL_TIMEOUT_MS` | 1000 | a call's timeout over a radio, or further than a neighbour (50..10000) |
| `CONFIG_SYS_SESSION_CALL_TIMEOUT_WIRED_MS` | 50 | a call's timeout to a neighbour on a cable (10..1000) |
| `CONFIG_SYS_SESSION_GONE_LISTENERS` | 5 | `sys_session_on_gone` and `sys_session_on_reached` slots, each |
| `CONFIG_SYS_SESSION_STREAM_LISTENERS` | 2 | `sys_session_on_stream` slots: the connectors, the watches' client |
| `CONFIG_SYS_SESSION_TIME_PERIOD_S` | 60 | the root sends its time to every board this often |
| `CONFIG_SYS_SESSION_TIMEOUT_MS` | 3000 | a child's session ends after this much silence (150..60000) |

## Needs elsewhere

- **errors.h**: `err_cmd_t` and `err_record_parse` (ERRORS.MD "Error record").
- **router**: `router_submit` with an answer callback, `router_inject`, `router_is_task` (ROUTER.MD); no links, no watch.
- **sys_system**: DEADMAN arms the dead-man and sets its time (`sys_session_set_timeout`); the dead-man is a gone listener; the root's sends STOP to every board; TIME on the root is broadcast to every board, when a board is reached and every `CONFIG_SYS_SESSION_TIME_PERIOD_S`.
- **The provider**: a framed one, opened by its one user; its driver's send with `NOWAIT` from any driver's RX task; its `WIRED` state (SYS_DATA_PROVIDER.MD "Driver").
- **The connector**: SUBSCRIBE / UNSUBSCRIBE by the command's `src`; its subscribers get `sys_session_stream`; it listens for gone peers and calls `sys_session_no_wait()` on its task.

## Tests

The host scripts (`router_link.py`, `.claude/skills/runit-esp/scripts/`) are a client: HELLO, PING, NO_SESSION, a new session after a restart, the streams subscribed. `router_tests.py` covers PEERS, ID, ATTACH refused, a request for another board, the dead-man by the session's silence; the app's backend (`runit-app/src/backend/session/link.ts`) is the BLE client, tested by `npm run test:hw`. `tree_tests.py` runs over two boards joined by the sys_uart link: the sessions on both ends, a ping and an ID read through the root, a board outside the tree, the root's clock on the child, a watch on the child whose entries come back through the root, the child's loss (`--loss`, the cable unplugged by hand). `remote_tests.py` runs the remote devices over the same two boards.

## Open

- The hello's descriptors' hash, board type and firmware are sent as 0.
- The users of the stream and reached listeners and of calls to other boards are the connectors' mirrors (SYS_DATA_CONNECTOR.MD), the watches' client (SYS_WATCH.MD) and sys_remote (SYS_REMOTE.MD); the board config's own calls to other boards are not written.
- OTA, the flasher: jobs on calls and streams (CMD.MD "Long work").
