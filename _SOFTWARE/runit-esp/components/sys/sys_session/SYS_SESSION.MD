# sys_session — sessions, the board's peers

Component `runit-esp/components/sys/sys_session/`. A **session** is the peer of a framed provider (SYS_DATA_PROVIDER.MD), a board or a client, known by its hello. It is **ports of the data matrix** (DATA_MATRIX.MD): its port, linked both ways with its provider, and a sub-port per module it shows. It adds little to a frame: this board's id as `src` when a frame has none, a PUSH's `seq`; its own are the hello, the ping, NO_SESSION and the dead-man, and it publishes who is reached and who is gone (events). **No routing, no task, no queue**: a frame goes out of the sub-port of its module and the links say where; what comes in runs on the sender's task (a driver's RX task, the router's task, a stream's sender), the timeouts on the board timers' task.

```
 provider ⇄ session port ─┬─ CONTROL: hello, ping, NO_SESSION (its own)
                          ├─ REQUEST, ANSWER ⇄ sub-port MODULE_ID_ROUTER ⇄ ROUTER (router_queue)
                          └─ PUSH (key m)    ⇄ sub-port m                ⇄ module m's port
```

## Files

| File | Role |
|---|---|
| `sys_session.h` | The session's wire format (the control ops, HELLO's body; the header, its kinds and ids are cmd.h's), the ports' name (`SYS_SESSION_PORT`), the use: start and this board; attach, detach, a peer's port, the peer of a port, wired, the dead-man, broadcast |
| `sys_session.c` | Everything except board id NVS and router handlers: the sessions, their ports' inputs, the control frames, the hello, the providers' events, the 10 ms tick, attach, detach and the dead-man |
| `sys_session_events.h` | The module's events: `SESSION_EV_REACHED`, `SESSION_EV_GONE`, and the filters of them |
| `internal/sys_session_internal.h` | For `sys_session.c` and `router/` only: `sys_session_list` |
| `router/sys_session_router.h` / `.c` | The remote interface (root key `MODULE_ID_SESSION` 0x0E): the frames, then `SESSION_OPERATIONS`; registers itself |
| `router/contract_session.json` | The router's descriptor |
| `errors/` | `MODULE_ID_SESSION` (0x0E), `ERR_SYS_SESSION_MAP`, the messages |
| `host/` | Host tests of the former session (Open) |

## Peers and ids

The ids are the header's sources (`CMD_SRC_*`, CMD.MD "The header"); this board's own, its boot id and its build are `board_id`'s (BOARD_ID.MD). A **board id** (0x01..0xEF) is a board's place in the project, set by the app (ID, kept in NVS), not its identity: a board swapped for another takes the same id and the project runs on (its new `boot_id` ends the old session). `CMD_SRC_NONE` (0x00) is **no id given yet**: a new board has it, and so does a board flashed by another; any number of sessions may have such a peer (they are told apart by their provider). A **client** (the app, a test host) picks its id in 0xF0..0xFC.

Every peer says its own id as the `src` of its hello, and no two open sessions have the same (`ERR_SESSION_ID_TAKEN`); an id nobody may say (`CMD_SRC_SELF`, `_ALL`) or this board's own is refused too. A board knows its peers only by its sessions: a session has the same rules whoever is on it (the app on BLE, a board on UART).

## The ports

`sys_session_attach(provider, deadman_ms, flags, modules, n)`, or the command `ATTACH`, makes a framed provider's peer a session (a raw one, or none: `ERR_SESSION_NOT_FRAMED`; a provider's id below 0x80):

- **its port** `SYS_SESSION_PORT(provider, 0)` (`DATA_MATRIX_PORT(DATA_MATRIX_PORT_SESSION | provider, 0)`), linked with the provider both ways by the attach itself;
- **a sub-port per module** of the list, `SYS_SESSION_PORT(provider, module)` (at most `CONFIG_SYS_SESSION_PORTS`): `MODULE_ID_ROUTER` for the commands, a stream's module for its PUSH frames. The sub-ports are linked with the modules' ports by the board's record (`CONNECT`): the command sub-port with one link, to `ROUTER_PORT` (the router's answers and calls come back to it by its name, no link back).

The same attach again is done (a stored record runs at every start); another time, flag or list on an attached provider is `ERR_CMD_BAD_ARG`. **A session lasts until `DETACH`** (`sys_session_detach(provider)`), the only way it goes: it ends the session and unregisters its ports (their links go). Whatever its provider does, the session stays attached and waits (the providers' events): DOWN ends it; REMOVED ends it too (the provider's links go with its port); CREATED again under the same id links the session port with it both ways (a raw one: `ERR_SESSION_NOT_FRAMED` reported, no link) and, when it is up, says the hello; UP says the hello. Its sub-ports, their links, its dead-man and its flag stay through all of it.

**From the line** (the session port's input, on the provider's RX task):

| Frame | Goes |
|---|---|
| `CONTROL` | the session's own (A session's life) |
| `REQUEST`, `ANSWER` | out of the sub-port `MODULE_ID_ROUTER`, unchanged; one whose `src` is not the peer's is dropped (a command is its sender's own, so the router's `src` and DEADMAN's session are the peer's) |
| `PUSH` | out of the sub-port of its `key`, unchanged; none: dropped |

Anything but HELLO and NO_SESSION on a session not open here is answered NO_SESSION and the hello.

**Onto the line** (a sub-port's input, on the sender's task): only on an open session (else dropped: a call through it ends by its timeout or its end). The frame is copied, its `src` set to this board's id when it is `CMD_SRC_NONE` (an id already there stays: a slave's frame passed on keeps the slave's), a PUSH's `seq` set (below), and it goes out of the session port with the sender's flags, a PUSH with `NOWAIT | SPARE` too, so telemetry never takes the room an answer or a control frame needs.

**A module and its peer**: `sys_session_port(peer, module, &port)` is the sub-port of `module` of the open session with `peer` (`ERR_SESSION_NO_ROUTE` without one): this board's calls to a board go to its `MODULE_ID_ROUTER` sub-port with `router_request` (ROUTER_QUEUE.MD "Calls"). `sys_session_peer(port, &peer)` is the peer of the session a port belongs to: a module that takes a packet learns its board from `from`. `sys_session_wired(board)`: the session with it runs on a `STATE_WIRED` provider (`sys_remote` waits for its answers only then).

### Counting push frames

A PUSH's `seq` is the session's count of the PUSH frames it sent in that **class**, normal or urgent (`URGENT` in the type byte: an urgent frame overtakes the normal ones in the provider's queue, so one counter for both would show false gaps). Every module's PUSH on the session shares the counter; a frame is counted when it goes onto the line, also when the provider drops it. The counters start at 0 with the session's attach. Whoever follows a board reads the gap itself: a `seq` that is not the last one of its class plus one is frames lost, and the first frame after its session opened tells nothing.

## A session's life

1. Attached, and when the provider is up (at attach, or its UP event), the board sends HELLO: `[hdr CONTROL, src its id, MODULE_ID_SESSION][HELLO][boot_id u32, build_id u32]`: `boot_id` random per boot, `build_id` the first 4 bytes of its firmware's ELF SHA-256 (whether a board runs the same build). A client sends its hello with its own id as `src` and `build_id` 0.
2. The session **opens** when the peer's hello has come (an id it may say, that no other open session has). A client is answered every hello (it may have missed ours: a BLE client not subscribed yet); a board its first only, so two boards do not answer each other for ever. `SESSION_EV_REACHED` is published. A refused hello changes nothing and is no sign of life.
3. Until then only HELLO and NO_SESSION pass. Anything else on a session not open here is answered NO_SESSION with the hello, so a peer that missed a restart (UART has no disconnect) or this board's timeout starts over at once. NO_SESSION from the peer ends this side too, and this board says hello. Every other frame of the peer is its sign of life.
4. A hello on an open session is its sign of life; a new `boot_id` or id ends it first: the peer restarted, so what it had here is gone.
5. **A session ends** when its provider goes down or is removed, its peer says a new `boot_id`, NO_SESSION comes, it is detached, or, with the dead-man on, nothing is received for its time: the calls through it fail (`router_request_fail_to` of its command sub-port: `ERR_ROUTER_CALL_DOWN`), `SESSION_EV_GONE` is published. The session stays attached, empty, until the next hello: a session never goes by itself, only by DETACH. Without the dead-man silence ends nothing: a peer that disappears with no sign is noticed by its calls timing out.
6. A session not open says hello again every second: the peer may have started after this board, and a board answers only the hello it gets.

One board timer (every 10 ms) does it all: the dead-man's pings and ends, the hellos.

## The dead-man

A session has a dead-man **of its own**: a time, or none. The attach sets it, `sys_session_deadman_set(peer, ms, safety)` changes it (sys_system's DEADMAN, for the session the command came on; `ms` 0: off), and the PEERS row says it. With a time:

- the session ends after that much silence;
- the **keepalive** is a third of it: the board sends a PING that often. Both sides with the same time keep each other alive; a client that set the time itself sends at least that often;
- with the flag `SYS_SESSION_SAFETY` its end is a **safety event**: `SESSION_EV_GONE` carries `safety` 1 and sys_system enters the safe state (SYS_SYSTEM.MD). Without the flag it only ends.

No time: nothing is sent and the silence ends nothing.

## To every session

`sys_session_broadcast(req, flags)` sends a command on every open session (STOP, the time): a REQUEST with `NOREPLY`, `seq` 0, out of each session port. The board that makes it does not run it: it has done what it broadcasts (a broadcast of the clock would make the board set its clock, and broadcast it, for ever).

## Reached and gone: events

The sessions publish two events (`sys_session_events.h`, module `MODULE_ID_SESSION`, source the peer's id, channel 0, inline): **`SESSION_EV_REACHED`** when a session opens and **`SESSION_EV_GONE`** `{safety}` when it ends. A listener subscribes with `sys_event_subscribe` and `SESSION_GONE_FILTER` / `SESSION_REACHED_FILTER` (`SYS_EVENT_RUN_INLINE`); it runs on the context that noticed it (a driver's RX task, the board timers' task, the events' task, the caller of detach), drops what it keeps for that peer and never blocks. The ones that listen: the watches (server and client), the remote devices and sys_system.

## Remote (`sys_session_router`, root key `MODULE_ID_SESSION` 0x0E)

The frames and the table are in `sys_session_router.h`; `contract_session.json` names them.

| Code | Command | Side | Request / answer |
|---|---|---|---|
| `0x01` | ID | read / write | `sys_session_id_args_t` `{u8 board_id}` (0: none, 1..0xEF): `board_id()`; the write is `board_id_save`, the board's from the next start |
| `0x02` | PEERS | read | → `u8 count`, then `sys_session_info_t` per session, 10 B: `u8 provider`, `u8 open`, `u8 peer_id` (`CMD_SRC_NONE` before its hello), `u8 safety`, `u32 build_id`, `u16 deadman_ms` (0: none) |
| `0x03` | ATTACH | write | `sys_session_attach_args_t` `{u8 provider, u8 flags, u16 deadman_ms}`, then a `u8` module per sub-port |
| `0x04` | DETACH | write | `sys_session_detach_args_t` `{u8 provider}` |

## Errors (`MODULE_ID_SESSION` 0x0E)

`ERR_SESSION_ID_TAKEN{board}`, `_NO_ROUTE{dst}`, `_FULL`, `_NOT_FRAMED`. A call's errors are the router's (ROUTER_QUEUE.MD "Calls").

## Limits

| Option | Default | Meaning |
|---|---|---|
| `CONFIG_SYS_SESSION_MAX` | 4 | sessions: one per provider |
| `CONFIG_SYS_SESSION_PORTS` | 4 | the sub-ports of a session (each a port of the data matrix) |

## Tests

On a board: `python runit-esp/tools/session_client.py <COM port>` opens a session as a client (0xF0) over the console and runs PING and PEERS; `--silence 4` leaves it silent and checks the board's dead-man ended the session.

## Open

- `host/test_sys_session.c` tests the former session (its provider hooks, calls and push): rewritten on the ports when the host runner is back in the repo.
- `session_client.py`, the router scripts and the app's session code speak the former frame: they move to the 5-byte header with the app's step.
- Two boards on a UART are not run on hardware yet.
