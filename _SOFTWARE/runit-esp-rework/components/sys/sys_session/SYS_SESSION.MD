# sys_session — sessions, the board's ways to its peers

Component `runit-esp-rework/components/sys/sys_session/`. A **session** is a provider (SYS_DATA_PROVIDER.MD) attached as a **way**: `UP` (to a client, or to this board's master) or `DOWN` (to a slave). The session layer owns the frames: it hands the commands for this board to the router (ROUTER_QUEUE.MD), forwards the frames for other boards without the router, carries the PUSH frames, makes the board's own calls and tells who is reached and who is gone. It has **no task and no queue** of its own: what comes in is handled on the driver's RX task, the timeouts on the board timers' task (TIMER.MD), and what goes out waits only in a driver's queue. Code knows a peer by its **id**: a board id, or a client's.

```
 provider (UART, BLE) ──► session (RX task) ─┬─ request for this board ──► router_submit ──► answer ──► session ──► provider
                                             ├─ frame for another board ──► the next hop's provider, as it is
                                             ├─ answer to a call of this board ──► its callback
                                             ├─ push ──► the push listeners (a channel's owner), or DATA to a provider
                                             └─ control: hello, ping, no session, peer
```

## Files

| File | Role |
|---|---|
| `sys_session.h` | Use: start and this board; attach, detach, wired; calls (call, call and wait); push, data; listeners (gone, reached, push) |
| `sys_session.c` | The sessions and the route, the frames out (two parts each), the listeners, opening and ending a session, PEER, the answers, the frames in, the tick, attach and detach |
| `config/sys_session_config.h` | The ids, the frame byte, the kinds, the channels, the control ops, the ways and the roles (published to the app) |
| `frame/sys_session_frame.h` | The header (`sys_session_hdr_t`), HELLO's body (`sys_session_hello_t`), PEER's (`sys_session_peer_t`) |
| `hello/sys_session_hello.c` | The handshake: this board's hello, the peer's, NO_SESSION |
| `call/sys_session_call.c` | The board's calls to other boards: their slots, answers, timeouts, the wait |
| `board/sys_session_board.c` | The board's identity, ESP-IDF's part: the id in NVS, a random id per boot, the firmware's build |
| `internal/sys_session_internal.h` | For the files above only: a session's slot and role, the frames out, the hello's and the calls' hooks |
| `router/sys_session_router.h` / `.c` | The remote interface (root key `MODULE_ID_SESSION` 0x0E): the frames, then `SESSION_OPERATIONS`; registers itself |
| `router/contract_session.json` | The router's descriptor |
| `errors/` | `MODULE_ID_SESSION` (0x0E, also `ERR_MOD_SYS_SESSION`), `ERR_SYS_SESSION_MAP`, the messages |
| `host/` | Host tests on two fake providers (Tests) |

## Boards, clients, the star

A **board id** (0x00..0xEF) is a board's place in the project, set by the app (ID, kept in NVS), not its identity: a board swapped for another takes the same id and the project runs on (its new `boot_id` ends the old session). `SYS_SESSION_ID_DEFAULT` (0x00) is **no id given yet**: a new board has it, and so does a board flashed by another (SYS_FLASHER.MD), so a board that takes slaves has another one.

The boards are a **star**, one level deep: the **root** and its slaves. The root is the board without a master, so a board alone is one; it is the board the app talks to. A board with a master has no other session, so a slave has no slaves and takes no client.

**The role is the way's and the hello's, not the build's**: every board runs the same firmware and attaches the same ways (`main`): its console's UART (and later the runIT BLE pair) `UP`, its link `DOWN`. The peer's role (`sys_session_role_e`) follows: a `DOWN` way leads to a **slave**; an `UP` way to the **master** when the hello carries a board's id (that way is another board's link wired to this board's console UART, SYS_UART.MD "The link to another board"), to a **client** when it carries a client's, `UNKNOWN` before the hello. A master's hello ends every other open session of the board (its peers are gone), and no other session opens while it lasts: their hellos are not answered. When it ends, the board is the root again and takes clients and slaves again.

**Clients** (the app, a test host) are not boards: only the root takes them. Every peer says its own id in its hello and no two may have one (`ERR_SESSION_ID_TAKEN`, also for an id of the wrong kind): a board's is 0x00..0xEF, the app's is the reserved `SYS_SESSION_ID_APP` (0xF0), any other client picks one in 0xF1..0xFC; the session checks that every frame from a client or a slave carries its id. `SYS_SESSION_ID_SELF` (0xFE) is the board itself as the source of its own commands (`ROUTER_SRC_SELF`): in error records, never in a frame's header.

The way is the sessions themselves: a slave sends everything to its master; the root sends to the open session whose hello gave that id.

| From | dst | Goes |
|---|---|---|
| anywhere | this board | to the router (a request), to my call (an answer), to the push listeners (a push), to the session (control) |
| a client | a slave of this board | to that slave, as it is |
| a slave | a client of this board | to that client, as it is |
| a slave | another slave | dropped, `ERR_SESSION_NO_ROUTE` to the source: the root relays nothing between slaves |
| a slave or a client | nobody this board has | dropped, `ERR_SESSION_NO_ROUTE` to the source |
| the master | anyone but this board | dropped, `ERR_SESSION_NO_ROUTE` to the source: a slave relays nothing |
| anywhere | `0xFF` broadcast | to the root, which runs it and sends it to every slave; each slave runs it; never answered. A broadcast the root itself makes (`sys_session_call`) is only sent to the slaves: the root has done what it broadcasts |

A frame so takes at most two hops and never loops. The slave does not know whether a request came from its master or from a client through it: it runs it as any command and answers its `src`.

## Frames

One header on every frame (`sys_session_hdr_t`, 5 B = `CMD_HEAD`): the frame byte, `dst`, `src`, `seq u16`. A board forwards a frame by its `dst` without reading or changing the rest. Every frame leaves as **two parts**, the header and the body (SYS_DATA_PROVIDER.MD "Use"), and a forwarded one as one part, as it came: nothing is copied to join them.

| Frame byte | Meaning |
|---|---|
| bits 0-1 | a request's or a push's flags: `ROUTER_FLAG_URGENT`, `_NOREPLY` (the router's own) |
| bit 2 | an answer's status: `router_status_e` (OK, ERROR) |
| bit 3 | `UNCOUNTED`: a PUSH outside its sender's counters, `seq` 0 (Counting push frames) |
| bits 4-5 | zero |
| bits 6-7 | the kind |

| Kind | After the header | `seq` |
|---|---|---|
| `REQUEST` | `[key][dir][code \| id + op][args]` | the source's counter |
| `ANSWER` | the data; ERROR's is the error record | the request's |
| `PUSH` | `[stream id][payload]`, or `[channel][data]` from `0x80`: `WATCHES` 0x80, `REMOTE` 0x81, `DATA` 0x82 (`[0x82][to id][from id][data]`, Data to a provider) | the sender's counter of (`dst`, class): a gap is frames lost |
| `CONTROL` | `op`, then its data: HELLO (`sys_session_hello_t`), PING, NO_SESSION, PEER (`sys_session_peer_t`) | 0 |

What follows the header is at most `CMD_DATA_MAX`. The frame size is the protocol's (CMD.MD "Frame size"), so nothing is sized per hop. Send flags: an answer waits for room in the provider's queue (the router's task) or, refused on the RX task, goes `NOWAIT`; every other frame goes `NOWAIT`, `URGENT` when its flags say so, and a **PUSH** with `SPARE` too (also one forwarded), so telemetry never takes the room an answer or a control frame needs.

## A session's life

1. `sys_session_attach(provider, way)`: the session opens the provider (a framed one only, else `ERR_SESSION_NOT_FRAMED`). The same way again on that provider is done, so a stored record that attaches runs at every start without an error; the other way on an attached provider is `ERR_CMD_BAD_ARG`.
2. When the provider is up (at attach, or its rx with `data` NULL, `len` 1), the board sends HELLO: its id, `boot_id` (random per boot, never 0), `build_id` (the first 4 bytes of its firmware's ELF SHA-256: whether a slave runs the same build), its timeout. A client sends its hello with its own id and `build_id` 0.
3. The session **opens** when the peer's hello has come: an id of its kind that no other session has, and no master of this board on another way. A client is answered every hello (it may have missed ours: a BLE client not subscribed yet); a board its first only, so two boards do not answer each other for ever. The reached listeners are told.
4. Until then only control frames pass. Anything else, and a PING, on a session not open here is answered `NO_SESSION` with the hello, so a peer that missed a restart (UART has no disconnect) or this board's timeout starts over at once; `ERR_SESSION_NONE` is reported once per closing of the session, not for every frame in flight. NO_SESSION from the peer ends this side too, and this board says hello.
5. A hello on an open session updates it (a new timeout); a new `boot_id` ends it first: the peer restarted, so what it had here is gone.
6. **Timeouts.** Each side ends the session after its own timeout of silence and says it in its hello; so each side sends something at least every third of the other's timeout, a PING when it has nothing else. A slave's session takes `CONFIG_SYS_SESSION_TIMEOUT_MS` (3 s); the master's and the clients' take the dead-man's time (`sys_session_set_timeout`, which sends a new hello on each). Nothing received for the timeout, or the provider down: the session **ends**: the calls through it fail (`ERR_SESSION_PEER_DOWN`), its peer is gone (Gone peers).
7. A `DOWN` way not open says hello again every second (on the root only): the board on it may have started after this one (flashed, a cable plugged in), and a board answers only the hello it gets.

One board timer (every 10 ms, the board timers' task) does it all: the pings, the hellos of a `DOWN` way, the session timeouts, the calls' timeouts. A session's timeout is at least 150 ms, a call's 10 ms.

## The root tells its peers: PEER

When a session of the root opens or ends, the root sends **PEER** `{id, up}` on every other open session (the slaves and the clients). A slave hears that a client went (`up` 0: what the client had on it ends, Gone peers); the app hears that a slave came (`up` 1: it adds its watches on it again) or went, at once and not by the silence of its watches. A slave takes PEER from its master only; `up` 1 is the app's to follow. One control frame each time, `NOWAIT`: one a full queue refused is lost.

## A request for this board

On the driver's RX task: the session is open (else NO_SESSION); then the command goes to the router (`router_submit`, copied, never blocks), with the session's framer as the callback of its answer. The router refused it (`ERR_ROUTER_QUEUE_FULL`, `_FRAME_LEN`): the session answers ERROR to its source at once (`NOWAIT`; not for NOREPLY) and reports it with the command. The answer (on the router task) finds the session towards `cmd->src` and sends the header with its status and the router's data, waiting for room at most the driver's bound. An error record names the command's `src` and `seq` (ERRORS.MD "Error record").

## Forwarding

A frame for another board is sent on from the driver's RX task it came in on: to the next hop's provider, as it is, with `NOWAIT` (and `URGENT`, `SPARE` by its frame byte). The driver's queue is the only queue it waits in; the router never sees it, and the board keeps no state of it (its answer comes back by the same rules, to `src`). A request that can't go on is answered to its source (unless NOREPLY) with why: the next hop's `ERR_PROV_BUSY`, `_DOWN` or `_TOO_LONG`, or `ERR_SESSION_NO_ROUTE`. An answer or a PUSH frame that can't go on is dropped (a reader times out and retries; a push shows a gap in its seq).

## The board's own calls

`sys_session_call(dst, req, flags, timeout_ms, cb, ctx)` runs a command and gives its answer to `cb`, a `router_done_f` (read-only data): one way to run a command with its answer, here or on another board.

- **To this board** (its id or `SYS_SESSION_ID_SELF`): `router_inject`: no slot, no timeout, the answer on the router task.
- **To another board**: the request goes out (`src` this board, `seq` its counter, `NOWAIT`, so a busy way is `ERR_PROV_BUSY` at once) and a call slot (`CONFIG_SYS_SESSION_CALLS`) keeps it until the answer. A call ends once: whoever ends it (the answer, the tick, the session ending, a waiter that gave up) takes it out of its slot under the calls' lock first, and only then is the callback called; a late answer finds no call and is dropped.

| Answer | `cb` gets |
|---|---|
| OK | the data |
| ERROR | that board's error record with `ERR_REMOTE{board, seq}` put before its chain, so `err_record_parse` gives this board's chain and the error policy's walk can stop at it |
| none in its timeout | ERROR: this board's record of `ERR_SESSION_TIMEOUT{dst, seq}` |
| the session it went through ends | ERROR: this board's record of `ERR_SESSION_PEER_DOWN{board}` |

**Every call to another board is answered**: `NOREPLY` goes between boards on a broadcast only (STOP, the time); a call to one board with it is `ERR_CMD_BAD_ARG`. **At most `CONFIG_SYS_SESSION_PEER_IN_FLIGHT` calls to one board are on the way at once**; one more is `ERR_SESSION_BUSY`: no failure, "later". **Timeouts follow the way**: `timeout_ms` 0 takes `CONFIG_SYS_SESSION_CALL_TIMEOUT_WIRED_MS` (50 ms) to the peer of a `WIRED` session (`sys_session_wired`), else `CONFIG_SYS_SESSION_CALL_TIMEOUT_MS` (1 s). A source retries a request after its timeout only when it is a read; a write without an answer is reported to whoever asked, who reads the state back.

### Waiting for the answer

`sys_session_call_wait(dst, req, flags, timeout_ms, buf, cap, &len)` is the same call, waited for: the answer in `buf`, or the error as a chain (`ERR_REMOTE` over that board's chain, `ERR_SESSION_TIMEOUT`, `_PEER_DOWN`; `ERR_CMD_BAD_LEN` for an answer longer than `cap`).

- The wait ends on a flag the call's callback sets, not on the task's notification: another notification of the waiting task (an urgent command, a timer, the power manager) wakes it, finds the flag down and sleeps again, so the wait never ends early and the callback never writes into a frame that is gone.
- **Its own limit**: the waiter sleeps at most the call's timeout and a margin (50 ms). Past it, it takes the call out of its slot itself and returns `ERR_SESSION_TIMEOUT`; if another task is ending it at that moment, it waits for that end. So a wait never hangs, also on a task the answer or the tick would need (the board timers' task: a driver's step that calls a remote device; the RX task of the way): it ends in its time instead.
- A call to this board on a router task is refused (`ERR_SESSION_WOULD_BLOCK`): the router would wait for itself.
- A router task may wait for another board: the commands behind it wait that long too. While a wait runs on the board timers' task, every board timer waits with it: a driver's step on a remote device is the exception, not the rule.

`sys_session_wired(peer)`: whether `peer` is the peer of an open session whose provider is `WIRED`: a neighbour on a cable.

## Push frames and data

A **PUSH** frame is what a board sends by itself, never answered: `sys_session_push(peer, data, len, flags)`, `[stream id or channel][payload]`, routed as any frame. A PUSH for this board goes to every push listener (`sys_session_on_push(fn(src, lost, data, len))`), each taking its own by the first byte. **DATA** (`sys_session_data(peer, to, from, data, len, flags)`) carries `[0x82][to id][from id][data]` so a provider of another board is written with no setup there: the board `dst` sends the data on its provider `to id`, no header, `NOWAIT`; what can't go is dropped. To a client, `to id` is a receiver it chose and `(src, from id)` names who sent it. A tunnel between boards is two streams, each sending to the other board's provider.

### Counting push frames

A PUSH needs no answer and no resynchronisation, so it has the simplest counter: the session's own, one per **pair (peer, class)**, the class being normal or urgent (`URGENT` in the frame byte: an urgent frame overtakes the normal ones in the provider's queue, so one counter for both would show false gaps). The modules (streams, watches, remote devices) send no `seq` of their own; every stream and channel to that peer shares the counter, DATA included. A gap tells how many frames of that class from that peer were lost, not of which stream: who follows a board refreshes what it follows of it.

- **By the other end's id, not by session**: a slave has one session and sends to the root and to each client of the root. The table (`CONFIG_SYS_SESSION_PUSH_PEERS` entries: `{peer, have, tx[2], rx[2]}`, 10 B each) holds both directions: this board's next `seq` to the peer, and the peer's last one to this board. A frame is counted when it is sent, also when the provider refuses it (the receiver sees the gap).
- **The receiver** (`lost` to the listeners): the gap between this frame's `seq` and the peer's last of its class, `uint16_t` wrapping; 0 for the first frame after the entry starts. A frame forwarded by the root is counted at its two ends only.
- **An entry starts again** when its peer is reached or gone: a session of this board opens or ends (the master's ending clears every entry: every peer was reached through it), and on a slave the master's PEER `{id, up}` either way.
- **A full table** never refuses a PUSH (a tunnel must not depend on a slave's table): it goes with `seq` 0 and `UNCOUNTED`, and the receiver tells `lost` 0 and leaves its counter as it is.

## Gone peers (and the dead-man)

A peer that can no longer be reached is **gone**, and every board learns it:

- A session of this board ends (silence past its timeout, the provider down, a new `boot_id`, NO_SESSION, detach): its peer is gone here. The master's session ending takes every peer: it is told once, with `peer` `SYS_SESSION_ID_BROADCAST` and role MASTER.
- A peer of the root goes: the root's PEER `{id, 0}` reaches every other peer; a slave tells its listeners with role `SYS_SESSION_VIA_NOTICE`.

Every listener (`sys_session_on_gone(fn(peer, role))`) is told on the context that noticed it (a driver's RX task, the board timers' task, the caller of detach); it drops what it keeps for that peer and never blocks. **The dead-man** needs no timer of its own: the silence of the master's or a client's session is it (sys_system's gone listener, SYS_SYSTEM.MD).

## Remote (`sys_session_router`, root key `MODULE_ID_SESSION` 0x0E)

The frames and the table are in `sys_session_router.h`; `contract_session.json` names them.

| Code | Command | Side | Request / answer |
|---|---|---|---|
| `0x01` | ID | read / write | `sys_session_id_args_t` `{u8 board_id}`; the write is kept in NVS and used from the next start |
| `0x02` | ATTACH | write | `sys_session_attach_args_t` `{u8 provider, u8 way}` (`sys_session_way_e`) |
| `0x03` | DETACH | write | `{u8 provider}` |
| `0x04` | PEERS | read | → `u8 count`, then `sys_session_info_t` per session, 11 B: `u8 provider`, `u8 way`, `u8 role` (`sys_session_role_e`), `u8 open`, `u8 peer_id` (`NONE` before its hello), `u32 build_id`, `u16` this board's timeout of it |

## Errors (`ERR_MOD_SYS_SESSION` = `MODULE_ID_SESSION` 0x0E)

`ERR_SESSION_NONE{src}`, `_ID_TAKEN{board}`, `_NO_ROUTE{dst}`, `_TIMEOUT{dst, seq}`, `_PEER_DOWN{board}`, `_FULL`, `_CALLS_FULL`, `ERR_REMOTE{board, seq}` (context: the chain under it is that board's), `_WOULD_BLOCK`, `_BUSY{dst}`, `_NOT_FRAMED`. The provider's own `ERR_PROV_*` pass as they are.

## Limits

| Option | Default | Meaning |
|---|---|---|
| `CONFIG_SYS_SESSION_MAX` | 4 | sessions: a master, slaves, the root's clients |
| `CONFIG_SYS_SESSION_CALLS` | 16 | the board's calls to other boards in flight |
| `CONFIG_SYS_SESSION_PEER_IN_FLIGHT` | 4 | of them to one board; one more: `ERR_SESSION_BUSY` |
| `CONFIG_SYS_SESSION_CALL_TIMEOUT_MS` | 1000 | a call's timeout over a radio, or further than a neighbour |
| `CONFIG_SYS_SESSION_CALL_TIMEOUT_WIRED_MS` | 50 | a call's timeout to a neighbour on a cable |
| `CONFIG_SYS_SESSION_BOARD_ID_DEFAULT` | 0 | the board's id when NVS keeps none |
| `CONFIG_SYS_SESSION_PEER_LISTENERS` | 5 | `sys_session_on_gone` and `sys_session_on_reached` slots, each |
| `CONFIG_SYS_SESSION_PUSH_LISTENERS` | 2 | `sys_session_on_push` slots |
| `CONFIG_SYS_SESSION_PUSH_PEERS` | 8 | peers with PUSH counters (both ways); past it a PUSH goes `UNCOUNTED` |
| `CONFIG_SYS_SESSION_TIMEOUT_MS` | 3000 | a slave's session ends after this much silence (150..60000) |

## Tests

`python test_host/run.py sys_session` (from `runit-esp`): two fake providers (an `UP` and a `DOWN` way), the router's and the board timers' tasks for real, the test as the peers. The hello and the session opening, a request answered to its source, frames on a closed session (NO_SESSION each, `ERR_SESSION_NONE` once), a PING on a closed session, a new `boot_id`, the silence past the timeout, the provider down, a board's hello on an `UP` way (the master: the slave's session ends; its PEER of a gone client), the root's PEER when a slave's session opens and ends, forwarding between a client and a slave, a push with `SPARE`, the push counters (per peer and class, DATA in the same one; the gaps told as `lost`, none after a peer's restart or an `UNCOUNTED` frame; a full table sends `UNCOUNTED` until the master's PEER frees an entry), a waited call that other notifications do not end (B1), one with no answer ending in its time, an ERROR answer under `ERR_REMOTE`, a call whose session ends (`ERR_SESSION_PEER_DOWN`).

On a board: `python runit-esp-rework/tools/session_client.py <COM port>` opens a session as the app (0xF0) over the console and runs PING and PEERS; `--silence 4` leaves it silent past its timeout and checks the board ended it; `--dst <board>` sends a command to a slave through this board.

## Open

- Two boards on the link (a slave reached through the root, the root's PEER to the app, the root's clock on the slave) are not run on hardware yet.
- The push counters run on a board with the first module that sends PUSH frames (the streams); until then only the host tests cover them.
