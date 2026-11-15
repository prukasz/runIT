# sys_session — sessions, the board's ways to its peers

Component `runit-esp-rework/components/sys/sys_session/`. A **session** is a provider (SYS_DATA_PROVIDER.MD) attached as a **way** to a peer: a **slave**, which ATTACH tells, or whoever the peer's hello says it is (a client, or this board's master). The session layer owns the frames: it hands the commands for this board to the router (ROUTER_QUEUE.MD), forwards the frames for other boards without the router, carries the PUSH frames, makes the board's own calls and tells who is reached and who is gone. It has **no task and no queue** of its own: what comes in is handled on the driver's RX task, the timeouts on the board timers' task (TIMER.MD), and what goes out waits only in a driver's queue. Code knows a peer by its **id**: a board id, or a client's.

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
| `sys_session.h` | The wire format (the ids, the frame byte, the kinds, the channels, the control ops and the peers, published to the app; the header `sys_session_hdr_t`, HELLO's body `sys_session_hello_t`, PEER's `sys_session_peer_t`), then the use: start and this board; attach, detach; calls (call, call and wait, broadcast); push, data; listeners (gone, reached, push) |
| `sys_session.c` | Everything except calls, board id NVS, and router handlers: the sessions, the route (`sys_session_route`), the one send (`send`: header + body as one flat frame), the control frames, the hello, answers, RX dispatch, forwarding, broadcast, push and data, the 10 ms tick, attach and detach |
| `internal/sys_session_internal.h` | For `sys_session.c`, `call/`, `board/`, and `router/` only: `sys_session_route`, `sys_session_send_request`, `sys_session_list`, and the calls' and the board's hooks |
| `call/sys_session_call.c` | The board's calls to other boards: their slots, answers, timeouts, the wait |
| `board/sys_session_board.c` | The board's identity, ESP-IDF's part: the id in NVS, a random id per boot, the firmware's build |
| `router/sys_session_router.h` / `.c` | The remote interface (root key `MODULE_ID_SESSION` 0x0E): the frames, then `SESSION_OPERATIONS`; registers itself |
| `router/contract_session.json` | The router's descriptor |
| `errors/` | `MODULE_ID_SESSION` (0x0E, also `ERR_MOD_SYS_SESSION`), `ERR_SYS_SESSION_MAP`, the messages |
| `host/` | Host tests on two fake providers (Tests) |

## Boards, clients, the star

A **board id** (0x00..0xEF) is a board's place in the project, set by the app (ID, kept in NVS), not its identity: a board swapped for another takes the same id and the project runs on (its new `boot_id` ends the old session). `SYS_SESSION_ID_DEFAULT` (0x00) is **no id given yet**: a new board has it, and so does a board flashed by another (SYS_FLASHER.MD), so a board that takes slaves has another one.

The boards are a **star**, one level deep: the **root** and its slaves. The root is the board without a master, so a board alone is one; it is the board the app talks to. A board with a master has no other session, so a slave has no slaves and takes no client.

**What a peer is comes from ATTACH and the hello, not from the build**: every board runs the same firmware and `app/runit` attaches the same two ways: its console (`provider_console`) and the runIT BLE pair, attached `UNKNOWN` (not told). The ways to slaves (a UART port with `provider_uart`, any framed provider) are the board's record's `ATTACH` frames, as many as the board has, attached `SLAVE`. The peer (`sys_session_peer_e`) follows: a way attached `SLAVE` leads to a **slave**; on one attached `UNKNOWN` the hello says: the **master** when it carries a board's id (that way is a way of another board wired to this board's console UART), a **client** when it carries a client's, `UNKNOWN` before the hello. A board's hello can't say whether it is a master or a slave, which is why only the slave is told. A master's hello ends every other open session of the board (its peers are gone), and no other session opens while it lasts: their hellos are not answered. When it ends, the board is the root again and takes clients and slaves again.

**Clients** (the app, a test host) are not boards: only the root takes them. Every peer says its own id in its hello and no two may have one (`ERR_SESSION_ID_TAKEN`, also for an id of the wrong kind): a board's is 0x00..0xEF, a client picks its own in 0xF0..0xFC (`SYS_SESSION_ID_CLIENT_FIRST`..`_LAST`); the session checks that every frame from a client or a slave carries its id. `SYS_SESSION_ID_SELF` (0xFE) is the board itself as the source of its own commands (`ROUTER_SRC_SELF`): in error records, never in a frame's header.

The way is the sessions themselves: a slave sends everything to its master; the root sends to the open session whose hello gave that id.

| From | dst | Goes |
|---|---|---|
| anywhere | this board | to the router (a request), to my call (an answer), to the push listeners (a push), to the session (control) |
| a client | a slave of this board | to that slave, as it is |
| a slave | a client of this board | to that client, as it is |
| a slave | another slave | dropped, `ERR_SESSION_NO_ROUTE` to the source: the root relays nothing between slaves |
| a slave or a client | nobody this board has | dropped, `ERR_SESSION_NO_ROUTE` to the source |
| the master | anyone but this board | dropped, `ERR_SESSION_NO_ROUTE` to the source: a slave relays nothing |
| anywhere | `0xFF` broadcast | run here (never answered) and sent on to every board next to this one but the one it came from: a slave's reaches the root, the root's its slaves. A broadcast a board itself makes (`sys_session_broadcast`) is only sent: it has done what it broadcasts |

A frame so takes at most two hops and never loops. The slave does not know whether a request came from its master or from a client through it: it runs it as any command and answers its `src`.

## Frames

One header on every frame (`sys_session_hdr_t`, 5 B = `CMD_HEAD`): the frame byte, `dst`, `src`, `seq u16`. A board forwards a frame by its `dst` without reading or changing the rest. Every frame leaves `sys_data_provider_send` as one flat `cmd_in_t` frame (SYS_DATA_PROVIDER.MD "Use"): a forwarded frame goes on as it came, with no copy; a frame this board makes is joined from its header and body once, in a buffer of its own size on the sender's stack (`send`).

| Frame byte | Meaning |
|---|---|
| bits 0-1 | the router's own flags: `ROUTER_FLAG_URGENT` on a request, on its answer and on a push; `_NOREPLY` on a request |
| bit 2 | an answer's status: `router_status_e` (OK, ERROR) |
| bits 3-5 | zero |
| bits 6-7 | the kind |

| Kind | After the header | `seq` |
|---|---|---|
| `REQUEST` | `[key][dir][code \| id + op][args]` | the source's counter |
| `ANSWER` | the data; ERROR's is the error record | the request's |
| `PUSH` | `[stream id][payload]`, or `[channel][data]` from `0x80`: a **channel** is a module's number with bit 7 set (`SYS_SESSION_CHANNEL(MODULE_ID_x)`: watches `SYS_WATCH_CHANNEL`, remote devices `SYS_REMOTE_CHANNEL`), and the session's own is `DATA` (`SYS_SESSION_CHANNEL_DATA`: `[DATA][to id][from id][data]`, Data to a provider) | the sender's count of its PUSH frames to `dst` in that class: a gap is frames lost |
| `CONTROL` | `op`, then its data: HELLO (`sys_session_hello_t`), PING, NO_SESSION, PEER (`sys_session_peer_t`) | 0 |

What follows the header is at most `CMD_DATA_MAX`. The frame size is the protocol's (CMD.MD "Frame size"), so nothing is sized per hop. The send flags follow the frame byte, the same for a frame this board makes and one it forwards: `NOWAIT`, `URGENT` by its bit, and `SPARE` for a **PUSH**, so telemetry never takes the room an answer or a control frame needs. The one exception: the router's answer (on the router's task) waits for room in the provider's queue.

## A session's life

1. `sys_session_attach(provider, peer)` (`SYS_SESSION_SLAVE` or `SYS_SESSION_UNKNOWN`): the session opens the provider (a framed one only, else `ERR_SESSION_NOT_FRAMED`). The same again on that provider is done, so a stored record that attaches runs at every start without an error; the other on an attached provider, and `MASTER` or `CLIENT`, are `ERR_CMD_BAD_ARG`.
2. When the provider is up (at attach, or its rx with `data` NULL, `len` 1), the board sends HELLO: its id, `boot_id` (random per boot, never 0), `build_id` (the first 4 bytes of its firmware's ELF SHA-256: whether a slave runs the same build), its timeout. A client sends its hello with its own id and `build_id` 0.
3. The session **opens** when the peer's hello has come: an id of its kind that no other session has, and no master of this board on another way. A client is answered every hello (it may have missed ours: a BLE client not subscribed yet); a board its first only, so two boards do not answer each other for ever. The reached listeners are told. A refused hello (`ERR_SESSION_ID_TAKEN`) changes nothing and is no sign of life.
4. Until then only HELLO and NO_SESSION pass. Anything else on a session not open here, and on an open one a frame whose `src` is not its peer's id (a client's and a slave's frames carry its own), is answered `NO_SESSION` with the hello, so a peer that missed a restart (UART has no disconnect) or this board's timeout starts over at once. NO_SESSION from the peer ends this side too, and this board says hello. Every other frame of the peer is its sign of life.
5. A hello on an open session updates it (a new timeout); a new `boot_id` or id ends it first: the peer restarted, so what it had here is gone.
6. **Timeouts.** Each side ends the session after its own timeout of silence and says it in its hello; so each side sends a PING every third of the other's timeout, whatever else it sends. A slave's session takes `CONFIG_SYS_SESSION_TIMEOUT_MS` (3 s); the master's and the clients' take the dead-man's time (`sys_session_set_timeout`, which sends a new hello on each). Nothing received for the timeout, or the provider down: the session **ends**: the calls through it fail (`ERR_SESSION_PEER_DOWN`), its peer is gone (Gone peers).
7. A way to a slave, not open, says hello again every second: the board on it may have started after this one (flashed, a cable plugged in), and a board answers only the hello it gets. A board with a master says hello on its master's way only.

One board timer (every 10 ms, the board timers' task) does it all: the pings, the hellos of the ways to slaves, the session timeouts, the calls' timeouts. A session's timeout is at least 150 ms (a shorter one in a hello is taken as 150).

## The root tells its peers: PEER

When a session of the root opens or ends, the root sends **PEER** `{id, up}` on every other open session (the slaves and the clients). A slave hears that a client went (`up` 0: what the client had on it ends, Gone peers); the app hears that a slave came (`up` 1: it adds its watches on it again) or went, at once and not by the silence of its watches. A slave takes PEER from its master only; `up` 1 is the app's to follow. One control frame each time, `NOWAIT`: one a full queue refused is lost, as every control frame is (a hello is said again, a ping follows).

## A request for this board

On the driver's RX task: the session is open (else NO_SESSION); then the command goes to the router (`router_submit`, copied, never blocks), with the session's framer as the callback of its answer. The router refused it (`ERR_ROUTER_QUEUE_FULL`, `_FRAME_LEN`): the session answers ERROR to its source at once (`NOWAIT`; not for NOREPLY) and reports it with the command. The answer (on the router task) takes the route to `cmd->src` and sends the header with its status, the request's `URGENT` and the router's data, waiting for room at most the driver's bound. An error record names the command's `src` and `seq` (ERRORS.MD "Error record").

## Forwarding

A frame for another board is sent on from the driver's RX task it came in on: to the next hop's provider, as it is, with the send flags of its frame byte. The driver's queue is the only queue it waits in; the router never sees it, and the board keeps no state of it (its answer comes back by the same rules, to `src`). A request that can't go on is answered to its source (unless NOREPLY) with why: the next hop's `ERR_PROV_BUSY` or `_DOWN`, or `ERR_SESSION_NO_ROUTE`. An answer or a PUSH frame that can't go on is dropped (a reader times out and retries; a push shows a gap in its seq).

## The board's own calls

`sys_session_call(dst, req, flags, timeout_ms, cb, ctx)` runs a command and gives its answer to `cb`, a `router_done_f` (read-only data): one way to run a command with its answer, here or on another board.

- **To this board** (its id or `SYS_SESSION_ID_SELF`): `router_inject`: no slot, no timeout, the answer on the router task.
- **To another board**: the route is taken (`ERR_SESSION_NO_ROUTE`), a call slot (`CONFIG_SYS_SESSION_CALLS`) keeps the call under a new `seq`, and the request goes out (`src` this board, `NOWAIT`, so a busy way is `ERR_PROV_BUSY` at once). A call ends once: whoever ends it (the answer with its `seq`, the tick, the session ending, a waiter that gave up) takes it out of its slot under the calls' lock first, and only then is the callback called; a late answer finds no call and is dropped.

| Answer | `cb` gets |
|---|---|
| OK | the data |
| ERROR | that board's error record with `ERR_REMOTE{board, seq}` put before its chain, so `err_record_parse` gives this board's chain and the error policy's walk can stop at it |
| none in its timeout | ERROR: this board's record of `ERR_SESSION_TIMEOUT{dst, seq}` |
| the session it went through ends | ERROR: this board's record of `ERR_SESSION_PEER_DOWN{board}` |

**Every call to another board is answered**: `NOREPLY` goes between boards on a broadcast only. **Timeout**: `timeout_ms`, or `CONFIG_SYS_SESSION_CALL_TIMEOUT_MS` (1 s) for 0: a caller that knows its way is a cable gives a short one. A source retries a request after its timeout only when it is a read; a write without an answer is reported to whoever asked, who reads the state back.

### Waiting for the answer

`sys_session_call_wait(dst, req, flags, timeout_ms, buf, cap, &len)` is the same call, waited for: the answer in `buf`, or the error as a chain (`ERR_REMOTE` over that board's chain, `ERR_SESSION_TIMEOUT`, `_PEER_DOWN`; `ERR_CMD_BAD_LEN` for an answer longer than `cap`).

- The wait ends on a flag the call's callback sets, not on the task's notification: another notification of the waiting task (an urgent command, a timer, the power manager) wakes it, finds the flag down and sleeps again, so the wait never ends early and the callback never writes into a frame that is gone.
- **Its own limit**: the waiter sleeps at most the call's timeout and a margin (50 ms). Past it, it takes the call out of its slot itself and returns `ERR_SESSION_TIMEOUT`; if another task is ending it at that moment, it waits for that end. So a wait never hangs, also on a task the answer or the tick would need (the board timers' task: a driver's step that calls a remote device; the RX task of the way): it ends in its time instead.
- A call to this board on a router task is refused (`ERR_SESSION_WOULD_BLOCK`): the router would wait for itself.
- A router task may wait for another board: the commands behind it wait that long too. While a wait runs on the board timers' task, every board timer waits with it: a driver's step on a remote device is the exception, not the rule.

`sys_session_wired(board)` says whether a call to a board leaves on a wired way now (its route's provider is `STATE_WIRED`): a remote device with `WIRED` waits for its answers only then (`sys_remote`).

### To every board

`sys_session_broadcast(req, flags)` sends a command to every board (STOP, the time): a REQUEST to `0xFF` with `NOREPLY`, `seq` 0. It goes on every open session whose peer is a board: from a slave to its master, from the root to its slaves. A board that gets one runs it and sends it on to every board next to it but the one it came from. The board that makes it does not run it: it has done what it broadcasts (a broadcast of the clock would make the root set its clock, and broadcast it, for ever).

## Push frames and data

A **PUSH** frame is what a board sends by itself, never answered: `sys_session_push(peer, data, flags)` (`cmd_in_t`, `[stream id or channel][payload]`), routed as any frame. A PUSH for this board goes to every push listener (`sys_session_on_push(fn(src, data, len))`), each taking its own by the first byte. **DATA** (`sys_session_data(peer, to, from, data, flags)`) carries `[SYS_SESSION_CHANNEL_DATA][to id][from id][data]` so a provider of another board is written with no setup there: the board `dst` sends the data on its provider `to id`, no header, `NOWAIT`; what can't go is dropped. To a client, `to id` is a receiver it chose and `(src, from id)` names who sent it. A tunnel between boards is two streams, each sending to the other board's provider.

### Counting push frames

A PUSH needs no answer and no resynchronisation, so its `seq` is the simplest counter: this board's count of the PUSH frames it sent to that **peer** in that **class**, normal or urgent (`URGENT` in the frame byte: an urgent frame overtakes the normal ones in the provider's queue, so one counter for both would show false gaps). The modules (streams, watches, remote devices) send no `seq` of their own; every stream and channel to that peer shares the counter, DATA included. A frame is counted when it is sent, also when the provider refuses it.

The counters are one array by peer id (`uint16_t[2]` each, about 1 KB): no table to fill, nothing to clear. They run on for as long as the board does. Whoever follows a board reads the gap itself: a `seq` that is not the last one of that `(src, class)` plus one is frames lost, of any stream or channel, and the first frame after its session with the board opened tells nothing. The firmware keeps no receive counters and tells its push listeners no gap.

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
| `0x02` | ATTACH | write | `sys_session_attach_args_t` `{u8 provider, u8 peer}` (`sys_session_peer_e`: `SLAVE` or `UNKNOWN`) |
| `0x03` | DETACH | write | `{u8 provider}` |
| `0x04` | PEERS | read | → `u8 count`, then `sys_session_info_t` per session, 10 B: `u8 provider`, `u8 peer` (`sys_session_peer_e`), `u8 open`, `u8 peer_id` (`NONE` before its hello), `u32 build_id`, `u16` this board's timeout of it |

## Errors (`ERR_MOD_SYS_SESSION` = `MODULE_ID_SESSION` 0x0E)

`ERR_SESSION_ID_TAKEN{board}`, `_NO_ROUTE{dst}`, `_TIMEOUT{dst, seq}`, `_PEER_DOWN{board}`, `_FULL`, `_CALLS_FULL`, `ERR_REMOTE{board, seq}` (context: the chain under it is that board's), `_WOULD_BLOCK`, `_NOT_FRAMED`. The provider's own `ERR_PROV_*` pass as they are.

## Limits

| Option | Default | Meaning |
|---|---|---|
| `CONFIG_SYS_SESSION_MAX` | 4 | sessions: a master, slaves, the root's clients |
| `CONFIG_SYS_SESSION_CALLS` | 16 | the board's calls to other boards in flight |
| `CONFIG_SYS_SESSION_CALL_TIMEOUT_MS` | 1000 | a call's timeout when its caller gives none |
| `CONFIG_SYS_SESSION_BOARD_ID_DEFAULT` | 0 | the board's id when NVS keeps none |
| `CONFIG_SYS_SESSION_PEER_LISTENERS` | 5 | `sys_session_on_gone` and `sys_session_on_reached` slots, each |
| `CONFIG_SYS_SESSION_PUSH_LISTENERS` | 2 | `sys_session_on_push` slots |
| `CONFIG_SYS_SESSION_TIMEOUT_MS` | 3000 | a slave's session ends after this much silence (150..60000) |

## Tests

`python test_host/run.py sys_session` (from `runit-esp`): two fake providers (a way not told and a way to a slave), the router's and the board timers' tasks for real, the test as the peers. The hello and the session opening, a request answered to its source, a frame on a session not open here and one with another's id (NO_SESSION and the hello), a PING on a closed session, a new `boot_id`, the silence past the timeout, the provider down, a board's hello on an `UP` way (the master: the slave's session ends; its PEER of a gone client), the root's PEER when a slave's session opens and ends, forwarding between a client and a slave, a request for a board not here (`ERR_SESSION_NO_ROUTE` to its source, every chain back in the pool), a broadcast (this board's to its slave only, a client's run and sent on, a slave's not back to it), a push with `SPARE`, the push `seq` per peer and class (DATA in the same one), a push to the listeners and DATA to its provider, a waited call that other notifications do not end (B1), one with no answer ending in its time, an ERROR answer under `ERR_REMOTE`, a call whose session ends (`ERR_SESSION_PEER_DOWN`), a hello's timeout below the least, a refused hello that does not keep the session alive.

On a board: `python runit-esp-rework/tools/session_client.py <COM port>` opens a session as a client (0xF0) over the console and runs PING and PEERS; `--silence 4` leaves it silent past its timeout and checks the board ended it; `--dst <board>` sends a command to a slave through this board.

## Open

- Two boards (a slave reached through the root, the root's PEER to the app, the root's clock on the slave) are not run on hardware yet.
