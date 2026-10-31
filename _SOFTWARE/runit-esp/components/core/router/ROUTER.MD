# router — the commands of this board (for agents)

Component `components/core/router/`. The router runs the commands of this board: it queues them, routes them through a tree of **routers** (a root table of keys, then each module's own router) on one of its two tasks, and hands every answer to the **callback** its command came with. The commands come from the **links** (UART, BLE: `router_link.c` makes a command of a link's frame and frames its answer) and from the board itself (`router_inject`).

## Files

| File | Role |
|---|---|
| `router.h` | Flags and statuses, command tables and op macros, register, route, init, submit, inject, tap, origin, is-task, urgent work, links. The handler type `cmd_f`, `cmd_reply` and `ERR_CMD_*` are in `components/core/cmd` (CMD.MD) |
| `errors/router_errors.h` | `ERR_ROUTER_MAP`, the module's error tags (included by `router.h`) |
| `errors/errors_router.json` | The error messages |
| `router_ids.h` | Root keys (`ROUTER_KEY_*`) |
| `router.c` | Root table, `router_route`, `router_call` / `router_call_op` / `router_args` / `router_args_tail` (op macros in `router.h`) |
| `router_dispatch.c` | `router_submit`, `router_inject`, the urgent and work tasks, the answers to the callbacks, tap, origin, `router_run_urgent` |
| `router_link.c` | The links: register, a link's frame to a command (`router_link_receive`), its answer to a frame |
| `router_priv.h` | Inside the component: reporting a command's chain |
| `Kconfig` | Response stream byte, table sizes, queues, task prio/stack |
| `AGENTS.md` | Rules for changing this folder |

## Wire format (a link's frames)

```
request   [seq][flags][root key][dir][code]...[payload]
response  [0x05][seq][status][data]        (CONFIG_ROUTER_RESPONSE_STREAM)
```

- `seq`: client's counter, echoed; the client matches answers by it (answers may come out of order — urgent commands overtake).
- `flags` (`ROUTER_FLAG_*`, `err_cmd_t.flags`): bit 0 `URGENT`, the urgent task (the app decides what is urgent; the firmware has no fixed list); bit 1 `NOREPLY`, no answer (a failure still goes to the errors stream).
- `dir` (`cmd_dir_e`, `cmd.h`): 0 `CMD_DIR_READ`, 1 `CMD_DIR_WRITE`, the side of the command that runs (CMD.MD "Read and write"). A module's command is `[key][dir][code][args]`; a device's `[0x05][dir][id u16][op u16][args]` (SYS_DEVICE.MD). A side the command doesn't have, or a dir above 1: `ERR_CMD_DIRECTION{dir}`.
- Stream frames (telemetry, events) have the command shape without `seq`, `flags` and `dir`: `[key][code][data]`.
- `status` 0 OK: `data` = router output (may be empty). 1 error: `data` = the **error record** (ERRORS.MD "Error record"): the request and its error chain together, so a captured or stored error is self-contained. Its command header is the command's `err_cmd_t` (`errors.h`).
- Payloads are packed little-endian structs, size checked exactly (`router_args`).

## Routing

- A module router is a `cmd_f` (CMD.MD): `in` starts at **this router's key**. The root calls `router_call(table, n, ctx, in, …)`, which looks up the key, calls that entry with the same `ctx`, and wraps a failure in `ERR_ROUTER_KEY{key}`. A module router consumes its key and calls `router_call_op(k_ops, n, ctx, {in.data + 1, in.len - 1}, out)` on `[dir][code][args]`: it finds the code's row, takes the side `dir` asks for (`cmd_side`), calls it with `[code][args]` and wraps a failure in `ERR_ROUTER_KEY{code}`. So every level shows in the error chain.
- **Command table**: `router_op_t {code, read, write}`, one row per command: `ROUTER_R(code, fn)`, `ROUTER_W(code, fn)` or `ROUTER_RW(code, read, write)` (NULL = no such side). What each side takes and answers: CMD.MD "Read and write".
- **Writing ops**: the native C API stays the source (typed, no packing on internal calls, callbacks and handles); an op only adapts it. Use the macros, not hand-written `cmd_f`s:
  - `ROUTER_OP(fn, args_t) { … }`: fixed packed payload, the body gets `args` (`const args_t*`).
  - `ROUTER_OP_NOARGS(fn) { … }`: no payload.
  - `ROUTER_OP_TAIL(fn, hdr_t) { … }`: header plus a variable tail (`tail`, a `cmd_in_t` of ≥ 1 byte), e.g. a NUL-ended name or data bytes.
  - `ROUTER_MODULE(fn, k_ops)`: writes the module's `cmd_f` over its command table (`router_call_op`; prefix `static` for a file-local one); `ROUTER_MODULE_DECLARE(fn);` declares it in the module's `*_router.h`.
  Bodies also see `ctx` and `out` (answer with `cmd_reply` / `cmd_append`). The length check (`ERR_CMD_BAD_LEN`) is done before the body runs. Remote-only policy (e.g. `ERR_I2C_REMOTE_DENIED`) belongs in the body, not in the native API.
- Root: `router_register(key, fn)` at boot (before `router_init`), from the app layer (`main`). Modules never register themselves; a module only exports its `cmd_f` (e.g. `sys_i2c_router` in `sys_i2c_router.h`).
- `out->cap` is `CMD_DATA_MAX` for every command; answers use `cmd_reply` (`ERR_CMD_OUT_FULL` when too big). A command answers once: data longer than that is paged by the command (CMD.MD "The handler").
- `router_route(in, out)` is also the entry point for replaying stored frames (actions, project), on the calling task.
- **Concurrency**: urgent and normal commands run concurrently on two tasks, so module routers must be thread-safe.

## Commands and answers

```
link RX task ── router_link_receive(link, frame) ─┐
board ───────── router_inject(req, flags, done) ──┼─ router_submit(cmd, done, ctx): copied into a ring
                                                  │    ├─ URGENT ─► urgent ring ─► urgent task (prio 15)
                                                  │    └─ else ───► work ring ───► work task (prio 5)
                                                  │                                  └─► router_route
done(ctx, cmd, status, data, len) ◄──── OK / ERROR ◄────────────────────────────────────┘
```

- **A command** is an `err_cmd_t` (`errors.h`): the request (`[key][dir]…`, 2..`CMD_DATA_MAX` bytes) and its origin (`link`, `seq`, `flags`).
- `router_submit(&cmd, done, ctx)` copies the command, its callback and `ctx` into the ring of its flags and returns; it never blocks, so a link's RX task calls it. A command it can't queue (bad length: `ERR_ROUTER_FRAME_LEN`, ring full: `ERR_ROUTER_QUEUE_FULL`) is not: the call returns why, and the caller answers it.
- `router_inject(req, flags, done, ctx)` is the same for the board's own commands: link `ROUTER_LINK_INTERNAL` (0), seq from the router's counter. The actions (`sys_actions_request`) and the project's start inject with no `done`.
- The router keeps nothing of who sent a command: the callback and its `ctx` do.
- **The answer**: each task runs a command and then its `done(ctx, cmd, status, data, len)` on the same task. `OK`: the handler returned NULL, `data` its answer (`out->len` bytes, maybe none). `ERROR`: `data` the error record (`err_record(cmd, chain)`). `data` is in the task's own buffer with `CMD_HEAD` bytes in front of it that are the callback's to write, so a link frames its answer without a copy.
- NOREPLY, or `done` NULL: no answer of any status. Every failure is reported with its command (log and errors stream), answered or not. A `done` that fails is reported and its answer lost.

### Tap, origin, urgent work

- **Tap** `router_set_tap(&{take, seen})` (boot, by the app): `take(cmd)` runs before routing — true = answered OK with no data and not routed; `seen(cmd, result)` after routing a command that was not taken. Both run on the routing task for every command (the board's own too). Used by the actions recorder (SYS_ACTIONS.MD).
- **Origin** `router_origin()`: the command the calling task is routing (link, seq, request), NULL outside the router tasks; inside a replay it is still the outer command. `router_is_task()`: the caller runs on a router task (what waits for an answer must not run there).
- **Urgent work** `router_run_urgent(fn)`: runs `fn` once on the urgent task, soon (any task or a timer callback, not an ISR); called again before it ran, it still runs once; another `fn` waiting → `ERR_ROUTER_BUSY`. For work that must not wait behind a command and may block: the safe state of `sys_system` (SYS_SYSTEM.MD).

## Links

A link is a whole transport, registered once: `router_link_register(id, &{send, max_frame, answer_endpoint})` at boot, never removed; `router_link_get(id)` gives one. The router's answers go to `answer_endpoint` (`send(answer_endpoint, frame, len, true)`), the data connectors send their streams to the endpoints they are bound to (SYS_DATA_CONNECTOR.MD). One client per link. `send` must be thread-safe (both router tasks and the connector task call it).

- `router_link_receive(id, frame, len)` (the link's RX task): makes the command of `[seq][flags][request]` and submits it with the link's answer as `done` (`[0x05][seq][status]` in front of the data, to `answer_endpoint`). A frame longer than `CMD_FRAME_MAX`, or one the router refuses, is answered with an error right away (when it has a seq) and reported with its command; `router_link_receive` then returns NULL.
- `router_link_on_frame(fn)` (boot, one): `fn` runs for every frame a link receives, before it is submitted, on the link's task: the dead-man of `sys_system` (a client is alive). The board's own commands don't pass here.

## Errors (`ERR_MOD_ROUTER` = 2)

| Error | Severity | Context / Fields | Description |
|---|---|---|---|
| `ERR_ROUTER_KEY` | NONE | `u8 key` | Route step context (wraps underlying handler error) |
| `ERR_ROUTER_NO_KEY` | HIGH | – | Input ended where a key was expected |
| `ERR_ROUTER_UNKNOWN_KEY` | HIGH | `u8 key` | No registered handler for key in table |
| `ERR_ROUTER_KEY_TAKEN` | HIGH | `u8 key` | Root key already registered |
| `ERR_ROUTER_TABLE_FULL` | HIGH | `u8 key` | Root table capacity (`CONFIG_ROUTER_MAX_KEYS`) reached |
| `ERR_ROUTER_BAD_LINK` | HIGH | `u8 link` | Invalid link ID, unregistered link or slot taken |
| `ERR_ROUTER_QUEUE_FULL` | MEDIUM | `u8 link, u8 urgent` | Ring full; the command was not queued |
| `ERR_ROUTER_FRAME_LEN` | MEDIUM | `u8 link, u16 len, u16 max` | A request shorter than 2 B (key, dir) or longer than `CMD_DATA_MAX`, or a link's frame longer than `CMD_FRAME_MAX` |
| `ERR_ROUTER_BUSY` | MEDIUM | – | `router_run_urgent`: another `fn` waits to run |

Payload and direction errors `ERR_CMD_BAD_LEN`, `_OUT_FULL`, `_BAD_ARG`, `_DIRECTION` belong to `ERR_MOD_CMD` = 11 (CMD.MD).

## Root keys and links

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

| Link (transport ID) | Component |
|---|---|
| `0` internal | the board itself (`router_inject`) |
| `1` UART | `sys/sys_uart` (SYS_UART.MD) — `#R:` hex lines |
| `2` BLE | `sys/sys_ble` (SYS_BLE.MD) — RX 0xFFE2 / TX 0xFFE1 |

## Limits

- `CMD_FRAME_MAX` (512 bytes, `cmd.h`, CMD.MD "Frame size"): the largest frame a link takes and sends; `CMD_DATA_MAX` the largest request and answer; the answer buffers hold `CMD_HEAD` + `CMD_DATA_MAX`.
- `CONFIG_ROUTER_MAX_KEYS` (default 16): maximum registered root keys.
- `CONFIG_ROUTER_MAX_LINKS` (default 4): maximum registered transport links (IDs 1..4); the data connectors bind to the same links, and `sys_data_connector.c` checks at build time that every `transport_e` fits.
- Command rings: `CONFIG_ROUTER_WORK_QUEUE_SIZE` (default 4 KB) and `CONFIG_ROUTER_URGENT_QUEUE_SIZE` (default 1 KB); each command takes its request, a 12-byte header (callback, `ctx`, origin) and the ring's own header.
- Concurrency: urgent task (priority 15) and work task (priority 5) run concurrently; module routers must be thread-safe.

## Tests

Host test script `router_tests.py` (in `.claude/skills/runit-esp/scripts/`) covers:
- The sys key (SYS_SYSTEM.MD): PING (uptime), STOP (safe state), DEADMAN (write, read back), BYE, TIME (write, read back), RESTART, SAFE / SAFE_LEAVE.
- The direction byte: a write-only command sent as a read, a read-only one as a write, a dir of 2 (`ERR_CMD_DIRECTION`), a frame without it (`ERR_ROUTER_FRAME_LEN`).
- Key dispatching, nested router calls, error chain recording with route keys.
- Urgent task preemption over work queue.
- Frame sizing, queue saturation, and error records.
