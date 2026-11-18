# sys_session — sessions, the board's links to its peers

Component `runit-esp-rework/components/sys/sys_session/`. A **session** is a framed provider (SYS_DATA_PROVIDER.MD) with one peer on it, a board or a client, set up with the peer's hello and kept until its role is cleared (data_matrix `CLEAR_ROLE`, `sys_session_detach`). It hands the peer's commands to the router (ROUTER_QUEUE.MD), carries the board's own calls to it and its PUSH frames, and publishes who is reached and who is gone (events). **There is no routing**: a frame goes to the peer on the provider it came on and nowhere else; to reach a board behind another, a command of the board in between does it (a call, a tunnel of providers). It has **no task and no queue** of its own: what comes in is handled on the driver's RX task, the timeouts on the board timers' task (TIMER.MD), and what goes out waits only in a driver's queue. Code knows a peer by its **id**, from its hello: a board id, or a client's.

```
 provider (UART, BLE) ──► session (RX task) ─┬─ request ──► router_submit ──► answer ──► session ──► provider
                                             ├─ answer to a call of this board ──► its callback
                                             ├─ push ──► the data matrix (`sys_session_set_push`: the routes of the tag)
                                             └─ control: hello, ping, no session
```

## Files

| File | Role |
|---|---|
| `sys_session_events.h` | The module's events: `SESSION_EV_REACHED`, `SESSION_EV_GONE`, and the filters of them |
| `sys_session.h` | The wire format (the ids, the frame byte, the kinds, the channels, the control ops, published to the app; the header `sys_session_hdr_t`, HELLO's body `sys_session_hello_t`), then the use: start and this board; attach, detach, the dead-man; calls (call, call and wait, broadcast); push and its taker |
| `sys_session.c` | Everything except calls, board id NVS, and router handlers: the sessions, the one send (`send`: header + body as one flat frame), the control frames, the hello, answers, RX dispatch, broadcast, push, the 10 ms tick, attach, detach and the dead-man |
| `internal/sys_session_internal.h` | For `sys_session.c`, `call/`, `board/`, and `router/` only: `sys_session_route`, `sys_session_send_request`, `sys_session_list`, and the calls' and the board's hooks |
| `call/sys_session_call.c` | The board's calls to other boards: their slots, answers, timeouts, the wait |
| `board/sys_session_board.c` | The board's identity, ESP-IDF's part: the id in NVS, a random id per boot, the firmware's build |
| `router/sys_session_router.h` / `.c` | The remote interface (root key `MODULE_ID_SESSION` 0x0E): the frames, then `SESSION_OPERATIONS`; registers itself |
| `router/contract_session.json` | The router's descriptor |
| `errors/` | `MODULE_ID_SESSION` (0x0E, also `MODULE_ID_SESSION`), `ERR_SYS_SESSION_MAP`, the messages |
| `host/` | Host tests on two fake providers (Tests) |

## Peers and ids

A **board id** (0x00..0xEF) is a board's place in the project, set by the app (ID, kept in NVS), not its identity: a board swapped for another takes the same id and the project runs on (its new `boot_id` ends the old session). `SYS_SESSION_ID_DEFAULT` (0x00) is **no id given yet**: a new board has it, and so does a board flashed by another (SYS_FLASHER.MD), so a board that takes slaves has another one.

A **client** (the app, a test host) is no board: its id is its pick in 0xF0..0xFC. Every peer says its own id in its hello, and no two open sessions have the same (`ERR_SESSION_ID_TAKEN`); an id nobody may say (`SYS_SESSION_ID_NONE`, `_SELF`, `_BROADCAST`) or this board's own is refused too. `SYS_SESSION_ID_SELF` (0xFE) is the board itself as the source of its own commands (`ROUTER_SRC_SELF`): in error records, never in a frame.

The board knows its peers only by the sessions it has, as many as it has providers attached: a session has the same rules whoever is on it (the app on BLE, a board on UART). Every board runs the same firmware and `app/runit` attaches the same two sessions, the console and the runIT BLE pair; a session with a board on a UART is the board's record's `ROLE` of that provider (DATA_MATRIX.MD). A frame a board gets from a session is that peer's: the command's `src` is its id.

## Frames

One header on every frame (`sys_session_hdr_t`, 3 B = `CMD_HEAD`): the frame byte and `seq u16`. Every frame leaves `sys_data_provider_send` as one flat `cmd_in_t` frame (SYS_DATA_PROVIDER.MD "Use"), joined from the header and the body at the send.

| Frame byte | Meaning |
|---|---|
| bits 0-1 | the router's own flags: `ROUTER_FLAG_URGENT` on a request, on its answer and on a push; `_NOREPLY` on a request |
| bit 2 | an answer's status: `router_status_e` (OK, ERROR) |
| bits 3-5 | zero |
| bits 6-7 | the kind |

| Kind | After the header | `seq` |
|---|---|---|
| `REQUEST` | `[key][dir][code \| id + op][args]` | the sender's counter |
| `ANSWER` | the data; ERROR's is the error record | the request's |
| `PUSH` | `[tag][data]`: a **tag** is a stream's id (below `0x80`) or, from `0x80`, a **channel**:  a module's number with bit 7 set (`SYS_SESSION_CHANNEL(MODULE_ID_x)`: watches `SYS_WATCH_CHANNEL`, remote devices `SYS_REMOTE_CHANNEL`); what the tag means is the data matrix's (DATA_MATRIX.MD) | the session's count of its PUSH frames of that class: a gap is frames lost |
| `CONTROL` | `op`, then its data: HELLO (`sys_session_hello_t`), PING, NO_SESSION | 0 |

What follows the header is at most `CMD_DATA_MAX`. The send flags follow the frame byte: `NOWAIT`, `URGENT` by its bit, and `SPARE` for a **PUSH**, so telemetry never takes the room an answer or a control frame needs. The one exception: the router's answer (on the router's task) waits for room in the provider's queue.

## A session's life

1. `sys_session_attach(provider, deadman_ms, flags)`: the session opens the provider (a framed one only, else `ERR_SESSION_NOT_FRAMED`) and stays on it until `sys_session_detach`, whoever is on it or not. A provider has one owner of its rx hook: the data matrix's `ROLE` `SESSION` calls attach, `CLEAR_ROLE` detach. `deadman_ms` 0: no dead-man; else its time, 150..60000 ms, and `flags` `SYS_SESSION_SAFETY` (below). The same again on that provider is done, so a stored record that attaches runs at every start without an error; another time or flag on an attached provider is `ERR_CMD_BAD_ARG`.
2. When the provider is up (at attach, or its rx with `data` NULL, `len` 1), the board sends HELLO: its id, `boot_id` (random per boot), `build_id` (the first 4 bytes of its firmware's ELF SHA-256: whether a board runs the same build), nothing else. A client sends its hello with its own id and `build_id` 0.
3. The session **opens** when the peer's hello has come (an id it may say, that no other open session has). A client is answered every hello (it may have missed ours: a BLE client not subscribed yet); a board its first only, so two boards do not answer each other for ever. `SESSION_EV_REACHED` is published. A refused hello (`ERR_SESSION_ID_TAKEN`) changes nothing and is no sign of life.
4. Until then only HELLO and NO_SESSION pass. Anything else on a session not open here is answered `NO_SESSION` with the hello, so a peer that missed a restart (UART has no disconnect) or this board's timeout starts over at once. NO_SESSION from the peer ends this side too, and this board says hello. Every other frame of the peer is its sign of life.
5. A hello on an open session is its sign of life; a new `boot_id` or id ends it first: the peer restarted, so what it had here is gone.
6. **A session ends** when its provider goes down, its peer says a new `boot_id`, NO_SESSION comes, it is detached, or, with the dead-man on, nothing is received for its time: the calls through it fail (`ERR_SESSION_PEER_DOWN`), its peer is gone (Gone peers). The session itself stays on the provider, empty, until the next hello. Without the dead-man silence ends nothing: a peer that disappears with no sign (power, a cable) is noticed by its calls timing out, not by the session.
7. A session not open says hello again every second: the peer on it may have started after this board (flashed, a cable plugged in), and a board answers only the hello it gets.

One board timer (every 10 ms, the board timers' task) does it all: the dead-man's pings and ends, the hellos, the calls' timeouts.

## The dead-man

A session has a dead-man **of its own**: a time, or none. `sys_session_attach(provider, deadman_ms, flags)` sets it at the start, `sys_session_deadman_set(peer, ms, safety)` changes it (sys_system's DEADMAN, for the session the command came on; `ms` 0: off), and the PEERS row says it. With a time:

- the session ends after that much silence (and stays on its provider, empty);
- the **keepalive** is a third of it: the board sends a PING that often. Both sides with the same time keep each other alive; a client (the app) that set the time itself sends at least that often;
- with the flag `SYS_SESSION_SAFETY` its end is a **safety event**: `SESSION_EV_GONE` carries `safety` 1 and sys_system enters the safe state (SYS_SYSTEM.MD). Without the flag it only ends.

No time: nothing is sent and the silence ends nothing.

## A request for this board

On the driver's RX task: the session is open (else NO_SESSION); then the command goes to the router (`router_submit`, copied, never blocks), with the session's framer as the callback of its answer and the peer's id as its `src`. The router refused it (`ERR_ROUTER_QUEUE_FULL`, `_FRAME_LEN`): the session answers ERROR to its source at once (`NOWAIT`; not for NOREPLY) and reports it with the command. The answer (on the router task) goes back on the open session with `cmd->src`, with its status, the request's `URGENT` and the router's data, waiting for room at most the driver's bound. An error record names the command's `src` and `seq` (ERRORS.MD "Error record").

## The board's own calls

`sys_session_call(dst, req, flags, timeout_ms, cb, ctx)` runs a command and gives its answer to `cb`, a `router_done_f` (read-only data): one way to run a command with its answer, here or on the peer with id `dst`.

- **To this board** (its id or `SYS_SESSION_ID_SELF`): `router_inject`: no slot, no timeout, the answer on the router task.
- **To a peer**: the open session whose peer is `dst` is taken (`ERR_SESSION_NO_ROUTE` when there is none), a call slot (`CONFIG_SYS_SESSION_CALLS`) keeps the call under a new `seq`, and the request goes out (`NOWAIT`, so a busy provider is `ERR_PROV_BUSY` at once). A call ends once: whoever ends it (the answer with its `seq`, the tick, the session ending, a waiter that gave up) takes it out of its slot under the calls' lock first, and only then is the callback called; a late answer finds no call and is dropped.

| Answer | `cb` gets |
|---|---|
| OK | the data |
| ERROR | that board's error record with `ERR_REMOTE{board, seq}` put before its chain, so `err_record_parse` gives this board's chain and the error policy's walk can stop at it |
| none in its timeout | ERROR: this board's record of `ERR_SESSION_TIMEOUT{dst, seq}` |
| the session it went through ends | ERROR: this board's record of `ERR_SESSION_PEER_DOWN{board}` |

**Every call to a peer is answered**: `NOREPLY` goes on a broadcast only. **Timeout**: `timeout_ms`, or `CONFIG_SYS_SESSION_CALL_TIMEOUT_MS` (1 s) for 0: a caller that knows its link is a cable gives a short one. A source retries a request after its timeout only when it is a read; a write without an answer is reported to whoever asked, who reads the state back.

### Waiting for the answer

`sys_session_call_wait(dst, req, flags, timeout_ms, buf, cap, &len)` is the same call, waited for: the answer in `buf`, or the error as a chain (`ERR_REMOTE` over that board's chain, `ERR_SESSION_TIMEOUT`, `_PEER_DOWN`; `ERR_CMD_BAD_LEN` for an answer longer than `cap`).

- The wait ends on a flag the call's callback sets, not on the task's notification: another notification of the waiting task (an urgent command, a timer, the power manager) wakes it, finds the flag down and sleeps again, so the wait never ends early and the callback never writes into a frame that is gone.
- **Its own limit**: the waiter sleeps at most the call's timeout and a margin (50 ms). Past it, it takes the call out of its slot itself and returns `ERR_SESSION_TIMEOUT`; if another task is ending it at that moment, it waits for that end. So a wait never hangs, also on a task the answer or the tick would need (the board timers' task: a driver's step that calls a remote device; the RX task of the provider): it ends in its time instead.
- A call to this board on a router task is refused (`ERR_SESSION_WOULD_BLOCK`): the router would wait for itself.
- A router task may wait for another board: the commands behind it wait that long too. While a wait runs on the board timers' task, every board timer waits with it: a driver's step on a remote device is the exception, not the rule.

`sys_session_wired(board)` says whether a call to a peer leaves on a wired provider now (its session's provider is `STATE_WIRED`): a remote device with `WIRED` waits for its answers only then (`sys_remote`).

### To every session

`sys_session_broadcast(req, flags)` sends a command on every open session (STOP, the time): a REQUEST with `NOREPLY`, `seq` 0. The board that makes it does not run it: it has done what it broadcasts (a broadcast of the clock would make the board set its clock, and broadcast it, for ever).

## Push frames

A **PUSH** frame is what a board sends by itself, never answered: `sys_session_push(provider, data, flags)` (`cmd_in_t`, `[tag][payload]`), on the open session of that provider (`ERR_SESSION_NOT_OPEN` without one); the data matrix's `T` end sends through it. A PUSH for this board goes to the one taker (`sys_session_set_push(fn(provider, data, len))`), which is the data matrix: it routes the payload by `(provider, tag)`. A module that has the peer's id finds the provider with `sys_session_route(peer, &provider)`, and the peer of a provider's session is `sys_session_peer(provider)`.

### Counting push frames

A PUSH needs no answer and no resynchronisation, so its `seq` is the simplest counter: the session's count of the PUSH frames it sent in that **class**, normal or urgent (`URGENT` in the frame byte: an urgent frame overtakes the normal ones in the provider's queue, so one counter for both would show false gaps). The modules (watches, remote devices) send no `seq` of their own; every channel on the session shares the counter. A frame is counted when it is sent, also when the provider refuses it. The counters are the session's: they start at 0 with it and run on for as long as it is attached. Whoever follows a board reads the gap itself: a `seq` that is not the last one of its class plus one is frames lost, and the first frame after its session opened tells nothing. The firmware keeps no receive counters and tells its push listeners no gap.

## Reached and gone: events

The sessions publish two events (`sys_session_events.h`, module `MODULE_ID_SESSION`, source the peer's id, channel 0, inline): **`SESSION_EV_REACHED`** when a session opens (its peer's hello came) and **`SESSION_EV_GONE`** `{safety}` when it ends (the dead-man's silence, the provider down, a new `boot_id`, NO_SESSION, detach). A listener subscribes with `sys_event_subscribe` and `SESSION_GONE_FILTER` / `SESSION_REACHED_FILTER` (inline: `SYS_EVENT_RUN_INLINE`); it runs on the context that noticed it (a driver's RX task, the board timers' task, the caller of detach), drops what it keeps for that peer and never blocks. `safety` 1: the session had the safety flag. The safety flag needs no timer of its own: the end of a session is it (sys_system's listener of `SESSION_EV_GONE`, SYS_SYSTEM.MD). The ones that listen: the watches (server and client), the remote devices and sys_system.

## Remote (`sys_session_router`, root key `MODULE_ID_SESSION` 0x0E)

The frames and the table are in `sys_session_router.h`; `contract_session.json` names them.

| Code | Command | Side | Request / answer |
|---|---|---|---|
| `0x01` | ID | read / write | `sys_session_id_args_t` `{u8 board_id}`; the write is kept in NVS and used from the next start |
| `0x02` | PEERS | read | → `u8 count`, then `sys_session_info_t` per session, 10 B: `u8 provider`, `u8 open`, `u8 peer_id` (`NONE` before its hello), `u8 safety`, `u32 build_id`, `u16 deadman_ms` (0: none) |

## Errors (`MODULE_ID_SESSION` 0x0E)

`ERR_SESSION_ID_TAKEN{board}`, `_NO_ROUTE{dst}`, `_TIMEOUT{dst, seq}`, `_PEER_DOWN{board}`, `_FULL`, `_CALLS_FULL`, `ERR_REMOTE{board, seq}` (context: the chain under it is that board's), `_WOULD_BLOCK`, `_NOT_FRAMED`. The provider's own `ERR_PROV_*` pass as they are.

## Limits

| Option | Default | Meaning |
|---|---|---|
| `CONFIG_SYS_SESSION_MAX` | 4 | sessions: one per provider |
| `CONFIG_SYS_SESSION_CALLS` | 16 | the board's calls to other boards in flight |
| `CONFIG_SYS_SESSION_CALL_TIMEOUT_MS` | 1000 | a call's timeout when its caller gives none |
| `CONFIG_SYS_SESSION_BOARD_ID_DEFAULT` | 0 | the board's id when NVS keeps none |

## Tests

`python test_host/run.py sys_session` (from `runit-esp`): two fake providers, the router's and the board timers' tasks for real, the test as the peers. The dead-man's time and flags checked at attach, the hello and the session opening, a request answered to its source, a frame on a session not open here (NO_SESSION and the hello), a PING on a closed session, a new `boot_id`, the dead-man's silence ending a session and leaving it empty, no silence without it, the provider down, sessions independent and an id in one of them, a call to a peer with no session (`ERR_SESSION_NO_ROUTE`), a broadcast on every open session, the dead-man's own time and keepalive (set, off, its end says safety), a PUSH taking only spare room, its `seq` per session and class, a PUSH taken with its provider and sent on the session of a provider (`ERR_SESSION_NOT_OPEN` without one), the calls and the wait (answered, timed out, an error under `ERR_REMOTE`, the session ending under it), a refused hello that is no sign of life.

On a board: `python runit-esp-rework/tools/session_client.py <COM port>` opens a session as a client (0xF0) over the console and runs PING and PEERS; `--silence 4` leaves it silent and checks the board's dead-man ended the session.

## Open

- Two boards on a UART are not run on hardware yet.
