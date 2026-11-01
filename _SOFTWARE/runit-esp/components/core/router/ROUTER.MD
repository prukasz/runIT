# router — the commands of this board (for agents)

Component `components/core/router/`. The router runs the commands of this board: it queues them, routes them through a tree of **routers** (a root table of keys, then each module's own router) on one of its two tasks, and hands every answer to the **callback** its command came with. It knows **commands, not frames**: the session (SYS_SESSION.MD) makes a command out of a frame and a frame out of an answer, so the header and the way back are the session's. The board's own commands come from `router_inject`.

```
 who sends                          router                                     module / device
 ─────────                          ──────                                     ───────────────
 session: frame → err_cmd_t ─┐
          router_submit      ├──►  urgent ring ─► urgent task ─┐
 board:   router_inject ─────┘     work ring ───► work task ────┼─ route ──►  cmd_f: answers now
                                                                │
 done(ctx, cmd, status, data) ◄──  OK / ERROR  ◄────────────────┘
   the session frames it to src; a call of the board to itself takes it
```

## Files

| File | Role |
|---|---|
| `router.h` | Flags and statuses, `ROUTER_SRC_SELF`, command tables and op macros, register, route, init, submit, inject, report, tap, origin, is-task, urgent work. The handler type `cmd_f`, `cmd_reply` and `ERR_CMD_*` are in `components/core/cmd` (CMD.MD) |
| `errors/router_errors.h` | `ERR_ROUTER_MAP`, the module's error tags (included by `router.h`) |
| `errors/errors_router.json` | The error messages |
| `router_ids.h` | Root keys (`ROUTER_KEY_*`) |
| `router.c` | Root table, `router_route`, `router_call` / `router_call_op` / `router_args` / `router_args_tail` (op macros in `router.h`) |
| `router_dispatch.c` | `router_submit`, `router_inject`, the urgent and work tasks, the answers to the callbacks, `router_report_cmd`, tap, origin, `router_run_urgent` |
| `Kconfig` | Table size, queues, task prio/stack |
| `AGENTS.md` | Rules for changing this folder |

## A command

A command is an `err_cmd_t` (`errors.h`, so an error record names it): the request `[key][dir][code | id + op][args]` and its origin:

| Field | Meaning |
|---|---|
| `src` | Who sent it: a client id (the session fills it from its frame), or `ROUTER_SRC_SELF` (0xFE: the board itself, `router_inject`) |
| `seq` | The sender's counter, `u16`; in its answer |
| `flags` | `ROUTER_FLAG_URGENT` (bit 0: the urgent task; the client decides what is urgent, the firmware has no fixed list), `ROUTER_FLAG_NOREPLY` (bit 1: no answer; a failure still goes to the errors stream) |

- `dir` (`cmd_dir_e`, `cmd.h`): 0 `CMD_DIR_READ`, 1 `CMD_DIR_WRITE`, the side of the command that runs (CMD.MD "Read and write"). A module's command is `[key][dir][code][args]`; a device's `[0x05][dir][id u16][op u16][args]` (SYS_DEVICE.MD). A side the command doesn't have, or a dir above 1: `ERR_CMD_DIRECTION{dir}`.
- Payloads are packed little-endian structs, size checked exactly (`router_args`).
- The flags are defined here only: the session's frame byte carries them at the same bits (SYS_SESSION.MD "Frames").

## Routing

- A module router is a `cmd_f` (CMD.MD): `in` starts at **this router's key**. The root calls `router_call(table, n, ctx, in, …)`, which looks up the key, calls that entry with the same `ctx`, and wraps a failure in `ERR_ROUTER_KEY{key}`. A module router consumes its key and calls `router_call_op(k_ops, n, ctx, {in.data + 1, in.len - 1}, out)` on `[dir][code][args]`: it finds the code's row, takes the side `dir` asks for (`cmd_side`), calls it with `[code][args]` and wraps a failure in `ERR_ROUTER_KEY{code}`. So every level shows in the error chain.
- **Command table**: `router_op_t {code, read, write}`, one row per command: `ROUTER_R(code, fn)`, `ROUTER_W(code, fn)` or `ROUTER_RW(code, read, write)` (NULL = no such side). What each side takes and answers: CMD.MD "Read and write".
- **Writing ops**: the native C API stays the source (typed, no packing on internal calls, callbacks and handles); an op only adapts it. Use the macros, not hand-written `cmd_f`s:
  - `ROUTER_OP(fn, args_t) { … }`: fixed packed payload, the body gets `args` (`const args_t*`).
  - `ROUTER_OP_NOARGS(fn) { … }`: no payload.
  - `ROUTER_OP_TAIL(fn, hdr_t) { … }`: header plus a variable tail (`tail`, a `cmd_in_t` of ≥ 1 byte), e.g. a NUL-ended name or data bytes.
  - `ROUTER_MODULE(fn, k_ops)`: writes the module's `cmd_f` over its command table (`router_call_op`; prefix `static` for a file-local one); `ROUTER_MODULE_DECLARE(fn);` declares it in the module's `*_router.h`.
  Bodies also see `ctx` and `out` (answer with `cmd_reply` / `cmd_append`). The length check (`ERR_CMD_BAD_LEN`) is done before the body runs. Remote-only policy (e.g. `ERR_I2C_REMOTE_DENIED`) belongs in the body, not in the native API.
- Root: `router_register(key, fn)` at boot (before `router_init`), from the app layer (`runit.c`). Modules never register themselves; a module only exports its `cmd_f` (e.g. `sys_i2c_router` in `sys_i2c_router.h`).
- `out->cap` is `CMD_DATA_MAX` for every command; answers use `cmd_reply` (`ERR_CMD_OUT_FULL` when too big). A command answers once: data longer than that is paged by the command (CMD.MD "The handler").
- `router_route(in, out)` is also the entry point for replaying stored frames (actions, project), on the calling task.
- **Concurrency**: urgent and normal commands run concurrently on two tasks, so module routers must be thread-safe.

## Commands and answers

- `router_submit(&cmd, done, ctx)` copies the command, its callback and `ctx` into the ring of its flags and returns; it never blocks, so the session calls it on a provider driver's RX task. A command it can't queue (a request shorter than 2 B or longer than `CMD_DATA_MAX`: `ERR_ROUTER_FRAME_LEN`; ring full: `ERR_ROUTER_QUEUE_FULL`) is not: the call returns why, and the caller answers it and reports it (`router_report_cmd`).
- `router_inject(req, flags, done, ctx)` is the same for the board's own commands: src `ROUTER_SRC_SELF`, seq from the router's counter. The actions (`sys_actions_request`) and the project's start inject with no `done`; `sys_session_call` to this board injects with its callback.
- The router keeps nothing of who sent a command: the callback and its `ctx` do. So there is no registry of links and no state per peer.
- **The answer**: each task runs a command and then its `done(ctx, cmd, status, data, len)` on the same task:

| Status | When | Data |
|---|---|---|
| `OK` | the handler returned NULL | the answer (`out->len` bytes, maybe none) |
| `ERROR` | the handler returned a chain | the error record (`err_record(cmd, chain)`, ERRORS.MD "Error record") |

- `data` is in the task's own buffer with `CMD_HEAD` bytes in front of it that are the callback's to write, so the session frames its answer without a copy. `done` may wait for room on the way back, at most its own bound (the session: its provider's driver).
- NOREPLY, or `done` NULL: no answer of any status. Every failure is reported with its command (`router_report_cmd`: the log line `cmd src f0 seq 7 flags 00 req …`, and the errors stream), answered or not. A `done` that fails is reported and its answer lost: a reader retries.

### Tap, origin, urgent work

- **Tap** `router_set_tap(&{take, seen})` (boot, by the app): `take(cmd)` runs before routing — true = answered OK with no data and not routed; `seen(cmd, result)` after routing a command that was not taken. Both run on the routing task for every command (the board's own too). Used by the actions recorder (SYS_ACTIONS.MD).
- **Origin** `router_origin()`: the command the calling task is routing (src, seq, request), NULL outside the router tasks; inside a replay it is still the outer command. A module that acts for a peer reads its `src` there: the connectors' SUBSCRIBE, the actions recorder. `router_is_task()`: the caller runs on a router task (what waits for an answer must not run there).
- **Urgent work** `router_run_urgent(fn)`: runs `fn` once on the urgent task, soon (any task or a timer callback, not an ISR); called again before it ran, it still runs once; another `fn` waiting → `ERR_ROUTER_BUSY`. For work that must not wait behind a command and may block: the safe state of `sys_system` (SYS_SYSTEM.MD). Whether a peer is alive is the session's (SYS_SESSION.MD "Gone peers").

## Errors (`ERR_MOD_ROUTER` = 2)

Codes 4, 5 and 8 are unused. A tag about a command carries its `src`.

| Error | Severity | Context / Fields | Description |
|---|---|---|---|
| `ERR_ROUTER_KEY` | NONE | `u8 key` | Route step context (wraps underlying handler error) |
| `ERR_ROUTER_NO_KEY` | HIGH | – | Input ended where a key was expected |
| `ERR_ROUTER_UNKNOWN_KEY` | HIGH | `u8 key` | No registered handler for key in table |
| `ERR_ROUTER_KEY_TAKEN` | HIGH | `u8 key` | Root key already registered |
| `ERR_ROUTER_TABLE_FULL` | HIGH | `u8 key` | Root table capacity (`CONFIG_ROUTER_MAX_KEYS`) reached |
| `ERR_ROUTER_QUEUE_FULL` | MEDIUM | `u8 src, u8 urgent` | Ring full; the command was not queued |
| `ERR_ROUTER_FRAME_LEN` | MEDIUM | `u8 src, u16 len, u16 max` | A request shorter than 2 B (key, dir) or longer than `CMD_DATA_MAX` |
| `ERR_ROUTER_BUSY` | MEDIUM | – | `router_run_urgent`: another `fn` waits to run |

Payload and direction errors `ERR_CMD_BAD_LEN`, `_OUT_FULL`, `_BAD_ARG`, `_DIRECTION` belong to `ERR_MOD_CMD` = 11 (CMD.MD).

## Root keys

| Key | Router | Doc |
|---|---|---|
| `0x01` sys | `sys_system_router` | SYS_SYSTEM.MD (ping, stop, restart, dead-man, clock, safe state) |
| `0x02` i2c | `sys_i2c_router` | SYS_I2C.MD "Remote" |
| `0x03` ble | `sys_ble_router` | SYS_BLE.MD "Remote" |
| `0x04` conn | `sys_data_connector_router` | SYS_DATA_CONNECTOR.MD "Remote" |
| `0x05` device | `sys_device_router` | SYS_DEVICE.MD: `[dir][id u16][op u16][args]` |
| `0x06` actions | `sys_actions_router` | SYS_ACTIONS.MD |
| `0x07` event | – (no router) | frame shape of the events stream: `[0x07][0x01 NOTIFY][sys_event_t]`, SYS_EVENT.MD |
| `0x08` log | `sys_log_router` | SYS_LOG.MD (log options CONFIG, error history ERRORS) |
| `0x09` | – | Reserved (unused) |
| `0x0A` power | `sys_power_manager_router` | SYS_POWER_MANAGER.MD |
| `0x0B` vm | `vm_router` | VM_CORE.MD (build, run and watch a program) |
| `0x0C` project | `sys_project_router` | SYS_PROJECT.MD: save a whole project as one stream, run, info, erase |
| `0x0D` provider | `sys_data_provider_router` | SYS_DATA_PROVIDER.MD "Remote" |
| `0x0E` session | `sys_session_router` | SYS_SESSION.MD "Remote" |
| `0x0F` watch | `sys_watch_router` | SYS_WATCH.MD "Remote" |

## Limits

- `CMD_FRAME_MAX` (512 bytes, `cmd.h`, CMD.MD "Frame size"): the largest frame; `CMD_DATA_MAX` the largest request and answer; the answer buffers hold `CMD_HEAD` + `CMD_DATA_MAX`. The router has no frame limit of its own.
- `CONFIG_ROUTER_MAX_KEYS` (default 16): maximum registered root keys.
- Command rings: `CONFIG_ROUTER_WORK_QUEUE_SIZE` (default 4 KB) and `CONFIG_ROUTER_URGENT_QUEUE_SIZE` (default 1 KB); each command takes its request, a 12-byte header (callback, `ctx`, origin) and the ring's own header.
- Concurrency: urgent task (priority 15) and work task (priority 5) run concurrently; module routers must be thread-safe.

## Tests

Host test script `router_tests.py` (in `.claude/skills/runit-esp/scripts/`, over a client session on the console: `router_link.py`) covers:
- The sys key (SYS_SYSTEM.MD): PING (uptime), STOP (safe state), DEADMAN (write, read back, the session's timeout, the safe state when the session ends), BYE, TIME (write, read back), RESTART, SAFE / SAFE_LEAVE.
- The direction byte: a write-only command sent as a read, a read-only one as a write, a dir of 2 (`ERR_CMD_DIRECTION`), a frame without it (`ERR_ROUTER_FRAME_LEN`).
- Key dispatching, nested router calls, error chain recording with route keys; the record names this client (`src`) and the seq.
- Urgent task preemption over work queue.
- Frame sizing, queue saturation, and error records; the providers, the sessions and the connectors' subscriptions.
