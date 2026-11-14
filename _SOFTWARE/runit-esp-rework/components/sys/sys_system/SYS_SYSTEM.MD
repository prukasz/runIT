# sys_system — system commands, the safe state, the dead-man

Component `runit-esp-rework/components/sys/sys_system/`. The board's system commands (root key `MODULE_ID_SYS` 0x28) and the two things they control: the **safe state** and the **dead-man**. It also sends the root's clock to every board.

| File | Role |
|---|---|
| `sys_system.h` / `.c` | The safe state, the dead-man, the time broadcast, the restart |
| `router/sys_system_router.h` / `.c` | The remote interface: `MODULE_ID_SYS`, the frames, `SYS_OPERATIONS`; registers itself |
| `router/contract_sys.json` | The router's descriptor |
| `Kconfig` | The dead-man at boot, the time broadcast's period |

## The safe state

`sys_system_enter_safe_state(reason)` suspends every device: `sys_device_sweep(SUSPEND)` (SYS_DEVICE.MD "Sweeps": the users before the devices they use; while it runs no write from outside starts an output it has not reached yet). A device's failure is reported, the others are suspended all the same. The reason (`sys_safe_reason_e`: STOP, DEADMAN, POWER) and the time are noted for SAFE. `sys_system_leave_safe_state()` (SAFE_LEAVE) resumes every device (`sys_device_sweep(RESUME)`, the used ones first) and clears the note; a device that fails to resume keeps the note. The sweep takes every device's lock, so neither runs inside a device's op: STOP runs on the router task that takes it (URGENT is the client's choice), the dead-man on the urgent task.

## The dead-man

The silence of the master's and the clients' sessions (SYS_SESSION.MD "Gone peers"). DEADMAN sets its time (`sys_system_deadman_set`): 0 is off and the sessions keep `CONFIG_SYS_SESSION_TIMEOUT_MS`; else it is their timeout (`sys_session_set_timeout`), told to the peer in a new hello. A session of the master or a client that ends while it is armed (silence that long, the link lost) enters the safe state: the gone listener notes the reason and has the router's urgent task run it (`router_run_urgent`). The root's dead-man also sends STOP to every board (a broadcast, URGENT, NOREPLY): a slave's own dead-man follows its master's session, which stays up when only the app is gone. BYE (the client leaving on purpose) disarms it. A command with no peer (a provider's command binding) can't arm it (`ERR_CMD_BAD_ARG`): no session whose silence could be noticed. Not stored: after a restart it is `CONFIG_SYS_SYSTEM_DEADMAN_DEFAULT_MS` again.

## The time

The time a client sets on the root (TIME) the root sends to every board as a broadcast, when a board is reached and every `CONFIG_SYS_SYSTEM_TIME_PERIOD_S` (a board timer), so every board's clock is the root's and an event's `t_ms` compares across boards. Nothing on a board with a master, or while no client has set the time.

## Remote (`sys_system_router`, root key `MODULE_ID_SYS` 0x28)

| Code | Command | Side | Request / answer |
|---|---|---|---|
| `0x01` | PING | read | → `u32` uptime in ms |
| `0x02` | STOP | write | – : the safe state, reason STOP |
| `0x03` | RESTART | write | – : the board restarts 100 ms after the answer (a board timer) |
| `0x04` | DEADMAN | read / write | `sys_system_deadman_t` `{u32 timeout_ms}` (0 = off) |
| `0x06` | BYE | write | – : the dead-man disarmed |
| `0x07` | TIME | read / write | `sys_system_time_t` `{u64 unix_ms}`; read 0 = not set since the power came on; the write is sent to every board |
| `0x09` | SAFE | read | → `sys_system_safe_t` `{u8 reason, u32 for_ms}`; reason 0 = not in the safe state |
| `0x0A` | SAFE_LEAVE | write | – : every device resumed, the note cleared |

0x05 and 0x08 are unused: never assign them.

## Limits

| Option | Default | Meaning |
|---|---|---|
| `CONFIG_SYS_SYSTEM_DEADMAN_DEFAULT_MS` | 0 | the dead-man at boot: 0, or the sessions' timeout (150..60000) |
| `CONFIG_SYS_SYSTEM_TIME_PERIOD_S` | 60 | the root sends its time to every board this often |

## Tests

On a board: `python runit-esp-rework/tools/session_client.py <COM port>` runs PING (`28 00 01`) over a session. STOP, SAFE and SAFE_LEAVE with devices, the dead-man by the session's silence, and the time on a slave are not run yet.

## Open

- The power manager's safe state (reason POWER) is wired when the power manager is started in `main`.
- The VM's stop joins the safe state when the VM is in the rework.
