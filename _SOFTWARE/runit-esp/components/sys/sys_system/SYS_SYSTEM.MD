# sys_system — system commands, safe state, dead-man

Component `components/sys/sys_system/`. The board's system commands under `ROUTER_KEY_SYS` (0x01) and the two things they control: the **safe state** (its note, the app's handler and way out) and the **dead-man** (a client's session that ends, SYS_SESSION.MD "Gone peers").

## Files

| File | Role |
|---|---|
| `sys_system.h` | `sys_safe_reason_e`, `SYS_SYSTEM_OP_RESTART`, the safe state and dead-man API, `sys_system_init` |
| `sys_system.c` | Safe state (handler, leave, note), dead-man (the session's gone peers, its time) |
| `router/sys_system_router.h` / `.c` | `sys_system_router` (`ROUTER_KEY_SYS`): the ops below |
| `router/contract_sys.json` | The service descriptor of the ops |
| `Kconfig` | `CONFIG_SYS_SYSTEM_DEADMAN_DEFAULT_MS` (dead-man at boot, default 0 = off) |
| `AGENTS.md` | Rules for changing this folder |

## Safe state

- `sys_system_set_safe_state(fn)`: the app's handler (`runit`: VM stopped, devices suspended, RUNIT.MD). Boot only. NULL = only logged.
- `sys_system_enter_safe_state(reason)`: notes the reason (`sys_safe_reason_e`) and the tick, then runs the handler on the caller's task. Callers: STOP (the router task that runs it), the dead-man (the router's urgent task), the power manager's response (`SYS_SAFE_POWER`, through `runit`).
- `sys_system_set_safe_leave(fn)`: the app's way out (every device resumed), run by `SAFE_LEAVE` through `sys_system_leave_safe_state`; the note is cleared only when it succeeds. The program is started again by the client (VM EXEC).
- `sys_system_safe_state_get(&reason, &for_ms)`: the note, for `SAFE` (0 = not in the safe state).

| `sys_safe_reason_e` | Value | Trigger |
|---|---|---|
| `SYS_SAFE_STOP` | 1 | sys STOP |
| `SYS_SAFE_DEADMAN` | 2 | a client's session ended while the dead-man is armed (silence past its time, its link lost) |
| `SYS_SAFE_POWER` | 4 | the power manager's response "safe state" |

3 is unused.

## Dead-man

The dead-man needs no timer of its own: it is the silence of a client's session. Runtime only, not stored: `sys_system_deadman_set(timeout_ms)`, 0 = off; at boot `CONFIG_SYS_SYSTEM_DEADMAN_DEFAULT_MS` (off). `sys_system_init` (boot, after `sys_session_init`) listens for gone peers (`sys_session_on_gone`) and arms the default.

- Armed, its time is the clients' sessions' timeout (`sys_session_set_timeout`, 150..60000 ms): the board's HELLO tells each client, which sends at least every third of it (a PING when it has nothing else). Off, the sessions keep `CONFIG_SYS_SESSION_TIMEOUT_MS`.
- A client's session that ends (nothing received for that long, or its provider down: a BLE disconnect) is a gone peer; armed, it enters the safe state (`SYS_SAFE_DEADMAN`). A session ends whether the dead-man is armed or not.
- The gone listener only notes the reason and calls `router_run_urgent()`; the router's urgent task enters the safe state, since the handler may block on I/O.
- `BYE` turns it off: the client leaves on purpose and the board goes on with what it runs.

## Remote (`ROUTER_KEY_SYS` = 0x01)

Requests are `[0x01][dir][op][args]` (`dir` 0 read, 1 write; ROUTER.MD). An RW op reads with no arguments and answers what its write takes; the write answers nothing.

| Op | Side | Write args / read answer |
|---|---|---|
| `0x01` PING | R | → `u32 uptime_ms` |
| `0x02` STOP | W | – (safe state, reason STOP) |
| `0x03` RESTART | W | – (restart ~100 ms after the answer) |
| `0x04` DEADMAN | RW | `u32 timeout_ms` (0 = off; else 150..60000, `ERR_CMD_BAD_ARG` outside) |
| `0x06` BYE | W | – (the client leaves on purpose: the dead-man is disarmed, the board goes on) |
| `0x07` TIME | RW | `u64 unix_ms` (UTC). Write: sets the board's clock (`board_time_set`, TIME.MD; no battery clock, lost with the power). Read: 0 while the clock is before 2020 (never set) |
| `0x09` SAFE | R | → `u8 reason, u32 for_ms` (`sys_safe_reason_e` of the last entry, 0 = not in the safe state; the time since) |
| `0x0A` SAFE_LEAVE | W | – (the app's leave: every device resumed; then the note cleared. A device that refuses to resume fails it and the note stays) |

Whether a command runs urgent is the client's choice (`ROUTER_FLAG_URGENT`). Payload errors are `ERR_CMD_*` (CMD.MD); the module has no error tags of its own. Sys frames are never recorded into actions (SYS_ACTIONS.MD).

Example from the client 0xF0: `01 00 f0 07 00 01 00 01` = REQUEST urgent, to board 0, seq 7; sys, read, PING → `40 f0 00 07 00 <uptime u32 LE>` (SYS_SESSION.MD "Frames").

## Tests

`router_tests.py` (`.claude/skills/runit-esp/scripts/`, build.md §6) covers every op, the dead-man and the safe state.
