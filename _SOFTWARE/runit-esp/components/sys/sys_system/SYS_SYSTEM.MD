# sys_system — system commands, safe state, dead-man

Component `components/sys/sys_system/`. The board's system commands under `ROUTER_KEY_SYS` (0x01) and the two things they control: the **safe state** (its note, the app's handler and way out) and the **dead-man** (the client's liveness, fed by the router's watch).

## Files

| File | Role |
|---|---|
| `sys_system.h` | `sys_safe_reason_e`, `SYS_SYSTEM_OP_RESTART`, the safe state and dead-man API, `sys_system_init` |
| `sys_system.c` | Safe state (handler, leave, note), dead-man (timer, config), the router watch |
| `sys_system_router.h` / `.c` | `sys_system_router` (`ROUTER_KEY_SYS`): the ops below |
| `contract_sys.json` | The service descriptor of the ops |
| `Kconfig` | `CONFIG_SYS_SYSTEM_DEADMAN_DEFAULT_MS` (dead-man at boot, default 0 = off) |
| `AGENTS.md` | Rules for changing this folder |

## Safe state

- `sys_system_set_safe_state(fn)`: the app's handler (`runit`: VM stopped, devices suspended, RUNIT.MD). Boot only. NULL = only logged.
- `sys_system_enter_safe_state(reason)`: notes the reason (`sys_safe_reason_e`) and the tick, then runs the handler on the caller's task. Callers: STOP (the router task that runs it), the dead-man and a lost link (the router's urgent task), the power manager's response (`SYS_SAFE_POWER`, through `runit`).
- `sys_system_set_safe_leave(fn)`: the app's way out (every device resumed), run by `SAFE_LEAVE` through `sys_system_leave_safe_state`; the note is cleared only when it succeeds. The program is started again by the client (VM EXEC).
- `sys_system_safe_state_get(&reason, &for_ms)`: the note, for `SAFE` (0 = not in the safe state).

| `sys_safe_reason_e` | Value | Trigger |
|---|---|---|
| `SYS_SAFE_STOP` | 1 | sys STOP |
| `SYS_SAFE_DEADMAN` | 2 | no frame within the dead-man timeout |
| `SYS_SAFE_LINK_LOST` | 3 | a link lost its client, with `on_link_lost` |
| `SYS_SAFE_POWER` | 4 | the power manager's response "safe state" |

## Dead-man

Runtime only, not stored: `sys_system_deadman_set(timeout_ms, on_link_lost)`, 0 = off; at boot `CONFIG_SYS_SYSTEM_DEADMAN_DEFAULT_MS` (off). `sys_system_init` (boot, before `router_init`) sets the router's watch (ROUTER.MD "Watch") and arms the default.

- Any frame from any link (`frame`) re-arms a one-shot timer; frames the board injects itself (link 0) do not.
- On expiry the safe state is entered once, until the next frame re-arms it.
- With `on_link_lost`, `sys_system_link_lost()` enters it at once. The app calls it when a link loses its client (`runit`: a BLE disconnect, through `sys_ble_add_conn_listener`).
- The timer and a lost link only note the reason and call `router_wake_urgent()`; the router's urgent task (`urgent`) enters the safe state, since the handler may block on I/O.
- `BYE` turns it off: the client leaves on purpose and the board goes on with what it runs.

## Remote (`ROUTER_KEY_SYS` = 0x01)

Requests are `[0x01][dir][op][args]` (`dir` 0 read, 1 write; ROUTER.MD). An RW op reads with no arguments and answers what its write takes; the write answers nothing.

| Op | Side | Write args / read answer |
|---|---|---|
| `0x01` PING | R | → `u32 uptime_ms` |
| `0x02` STOP | W | – (safe state, reason STOP) |
| `0x03` RESTART | W | – (restart ~100 ms after the answer) |
| `0x04` DEADMAN | RW | `u32 timeout_ms, u8 on_link_lost` |
| `0x06` BYE | W | – (the client leaves on purpose: the dead-man is disarmed, the board goes on) |
| `0x07` TIME | RW | `u64 unix_ms` (UTC). Write: the system clock (`settimeofday`; no battery clock, lost with the power). Read: 0 while the clock is before 2020 (never set) |
| `0x09` SAFE | R | → `u8 reason, u32 for_ms` (`sys_safe_reason_e` of the last entry, 0 = not in the safe state; the time since) |
| `0x0A` SAFE_LEAVE | W | – (the app's leave: every device resumed; then the note cleared. A device that refuses to resume fails it and the note stays) |

Whether a command runs urgent is the client's choice (`ROUTER_FLAG_URGENT`). Payload errors are `ERR_CMD_*` (CMD.MD); the module has no error tags of its own. Sys frames are never recorded into actions (SYS_ACTIONS.MD).

Example over UART: `#R:0701010001` = seq 7, flags 1 (urgent), sys, read, PING → `#R:050700<uptime LE>`.

## Tests

`router_tests.py` (`.claude/skills/runit-esp/scripts/`, build.md §6) covers every op, the dead-man and the safe state.
