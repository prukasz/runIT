# sys_system — system commands, the safe state, the dead-man

Component `runit-esp/components/sys/sys_system/`. The board's system commands (root key `MODULE_ID_SYS`) and the two things they control: the **safe state** and the **dead-man**. It also sends the root's clock to every board.

| File | Role |
|---|---|
| `sys_system.h` / `.c` | The safe state, the dead-man, the time broadcast, the restart |
| `sys_system_events.h` | The events table (`SYS_EVENTS`: SAFE) and `SYS_SAFE_FILTER` |
| `router/sys_system_router.h` / `.c` | The remote interface: `MODULE_ID_SYS`, the frames, `SYS_OPERATIONS`; registers itself |
| `router/contract_sys.json` | The router's descriptor |
| `Kconfig` | The dead-man at boot, the time broadcast's period |

## The safe state

`sys_system_enter_safe_state(reason, device)` is the one way into the safe state, whoever asks for it:

| Reason (`sys_safe_reason_e`) | Who | Task |
|---|---|---|
| STOP | the STOP command | the router task that takes it (URGENT is the client's choice) |
| DEADMAN | a session with the safety flag ended (below) | the urgent task |
| POWER | the power manager's response SAFE_STATE (SYS_POWER_MANAGER.MD); `device` the rail's regulator, or none for the board's own | the power manager's (its queued listener) |
| FAULT | a device's critical error (SYS_DEVICE_POLICY.MD); `device` the device, or none for an error above every device | the policy's task |

It notes the reason and the time (for SAFE), publishes the event **SAFE** inline, then suspends every device: `sys_device_sweep(SUSPEND)` (SYS_DEVICE.MD "Sweeps": the users before the devices they use; while it runs no write from outside starts an output it has not reached yet). A device's failure does not stop the others' suspend; the first error is returned and the caller reports it (the policy wraps it as its response's failure). `sys_system_leave_safe_state()` (SAFE_LEAVE) resumes every device (`sys_device_sweep(RESUME)`, the used ones first) and clears the note; a device that fails to resume keeps the note. The sweep takes every device's lock, so neither runs inside a device's op.

**The event SAFE** (`sys_system_events.h`, module `MODULE_ID_SYS`, lane SYS, NOTIFY): channel = the reason, source = `device` (`SYS_DEVICE_ID_NONE` when none), no payload. Its inline listeners run before the sweep, on the task that enters the safe state, so what writes to the devices stops first: the VM asks its program to stop (VM_CORE.MD "Safe state"). A listener only asks for its stop and never waits for it: the one who writes may be inside a device op that waits for this very task. NOTIFY: it goes out on the event stream (SYS_EVENT_STREAM.MD) like every notify event.

## The dead-man

A session has a dead-man of its own (SYS_SESSION.MD "The dead-man"): at ATTACH (`app/runit` gives the console's and the BLE session's `CONFIG_SYS_SYSTEM_DEADMAN_DEFAULT_MS`, with the safety flag when it is not 0), and DEADMAN sets the time of the session the command came on, with the safety flag (`sys_session_deadman_set`): 0 is off and that session never ends by silence; else it is its silence limit, and a third of it the keepalive. A session with the safety flag that ends (silence that long, the link lost) enters the safe state: the listener of `SESSION_EV_GONE` (`safety` 1) notes the reason and has the router's urgent task run it (`router_run_urgent`), and sends STOP on every other open session (a broadcast, URGENT, NOREPLY). BYE turns the dead-man of its session off: the client leaves on purpose.

## The time

A board that has a time (a client set it with TIME) sends it on every open session as a broadcast, when a peer is reached and every `CONFIG_SYS_SYSTEM_TIME_PERIOD_S` (a board timer), so the clocks of the boards that talk agree and an event's `t_ms` compares across them. Nothing while no client has set the time.

## Remote (`sys_system_router`, root key `MODULE_ID_SYS`)

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
| `0x0B` | TASKS | read | → `[u8 count]` and `count` × `sys_system_task_t` `{char name[16], u16 stack_free, u8 priority}`: every task, as many as one answer holds; `stack_free` is the least stack it has had free since it started, in bytes (the margin of the deepest call chains, DATA_MATRIX.MD "Open"). Needs `CONFIG_FREERTOS_USE_TRACE_FACILITY` |

0x05 and 0x08 are unused: never assign them.

## Limits

| Option | Default | Meaning |
|---|---|---|
| `CONFIG_SYS_SYSTEM_DEADMAN_DEFAULT_MS` | 0 | the dead-man at boot: 0, or the sessions' timeout (150..60000) |
| `CONFIG_SYS_SYSTEM_TIME_PERIOD_S` | 60 | a board sends its time on every open session this often |

## Tests

Host tests in `host/` (`python test_host/run.py sys_system` from `runit-esp/`, `HOST_TESTS.MD`), with the real `router`, `timer`, `sys_event` and `data_matrix` and a stand-in session (`sys_session_broadcast`, `sys_session_on_gone`): the safe state entered and left, SAFE heard inline before the sweep with its reason and device, a failed resume keeping its note; `SESSION_EV_GONE` with its safety bit starting the dead-man and the urgent STOP broadcast; BYE, PING, the time and TASKS through the router.

On a board: `python runit-esp/tools/session_client.py <COM port>` runs PING (`28 00 01`) over a session. STOP, SAFE and SAFE_LEAVE with devices, the dead-man by the session's silence, and the time on a slave are not run yet.

## Open

- The power manager is not started in `app/runit` yet, so the reason POWER does not occur on a board.
