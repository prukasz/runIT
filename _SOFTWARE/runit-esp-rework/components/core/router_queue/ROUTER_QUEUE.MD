# router_queue — the commands of this board in a queue (for agents)

Component `components/core/router_queue/`. It runs the commands of this board: it queues them, routes them (`router_route`, ROUTER.MD) on one of its two tasks, and hands every answer to the **callback** its command came with. It knows **commands, not frames**: the session (SYS_SESSION.MD) makes a command out of a frame and a frame out of an answer, so the header and the way back are the session's. The board's own commands come from `router_inject`.

```
 who sends                          router                                     module / device
 ─────────                          ──────                                     ───────────────
 session: frame → cmd_req_t ─┐
          router_submit      ├──►  urgent ring ─► urgent task ─┐
 board:   router_inject ─────┘     work ring ───► work task ────┼─ route ──►  cmd_f: answers now
                                                                │
 done(ctx, cmd, status, data) ◄──  OK / ERROR  ◄────────────────┘
   the session frames it to src; a call of the board to itself takes it
```

## Files

| File | Role |
|---|---|
| `router_queue.h` | The flags and statuses (of a command, `cmd_req_t` in `utils/cmd_types.h`; its error record is `err_record_cmd`, `err_record.h`), `ROUTER_SRC_SELF`, `ROUTER_SRC_NONE`, `router_init`, submit, inject, report, origin, is-task, urgent work. The routing, the handler and the op tables are `router.h` (ROUTER.MD) |
| `errors/router_queue_errors.h`, `errors/errors_router_queue.json` | `ERR_ROUTER_QUEUE_MAP` (`MODULE_ID_ROUTER_QUEUE`) and the messages |
| `router_queue.c` | `router_submit`, `router_inject`, the urgent and work tasks, the answers to the callbacks, `router_report_cmd`, origin, `router_run_urgent`, `router_init` |
| `Kconfig` | Queues, task prio and stack |
| `host/` | Host tests (`test_router_queue.c`, `test_router_tasks.c`, `host_test.json`, `router_host_config.h`; see Tests); not part of the firmware build |
| `AGENTS.md` | Rules for changing this folder |

## A command

A command is a `cmd_req_t` (`cmd_types.h` in `utils`; an error record names it): the request `[key][dir][code | id + op][args]` and its origin:

| Field | Meaning |
|---|---|
| `src` | Who sent it: a client id (the session fills it from its frame), `ROUTER_SRC_SELF` (0xFE: the board itself, `router_inject`), or `ROUTER_SRC_NONE` (0xFD: no peer, a provider's command binding, SYS_DATA_PROVIDER.MD) |
| `seq` | The sender's counter, `u16`; in its answer |
| `flags` | `ROUTER_FLAG_URGENT` (bit 0: the urgent task; the client decides what is urgent, the firmware has no fixed list), `ROUTER_FLAG_NOREPLY` (bit 1: no answer; a failure still goes to the errors stream) |

- `dir` (`cmd_dir_e`, `router_queue.h`): 0 `CMD_DIR_READ`, 1 `CMD_DIR_WRITE`, the side of the command that runs (ROUTER.MD "Read and write"). A module's command is `[key][dir][code][args]`; a device's `[0x05][dir][id u16][op u16][args]` (SYS_DEVICE.MD). A side the command doesn't have, or a dir above 1: `ERR_CMD_DIRECTION{dir}`.
- Payloads are packed little-endian structs, size checked exactly (`CMD_HANDLER`, ROUTER.MD "Routing").
- The flags are defined here only: the session's frame byte carries them at the same bits (SYS_SESSION.MD "Frames").

## Commands and answers

- `router_submit(&cmd, done, ctx)` copies the command, its callback and `ctx` into the ring of its flags and returns; it never blocks, so the session calls it on a provider driver's RX task. A command it can't queue (a request shorter than 2 B or longer than `CMD_DATA_MAX`: `ERR_ROUTER_FRAME_LEN`; ring full: `ERR_ROUTER_QUEUE_FULL`) is not: the call returns why, and the caller answers it and reports it (`router_report_cmd`).
- `router_inject(req, flags, done, ctx)` is the same for the board's own commands: src `ROUTER_SRC_SELF`, seq from the router's counter. The actions (`sys_actions_request`) and the project's start inject with no `done`; `sys_session_call` to this board injects with its callback.
- The router keeps nothing of who sent a command: the callback and its `ctx` do. So there is no registry of links and no state per peer.
- **The answer**: each task runs a command and then its `done(ctx, cmd, status, data, len)` on the same task:

| Status | When | Data |
|---|---|---|
| `OK` | the handler returned NULL | the answer (`out->len` bytes, maybe none) |
| `ERROR` | the handler returned a chain | the error record (`err_record_cmd(cmd, chain)`, ERRORS.MD "Error record") |

- `data` is in the task's own buffer (`CMD_DATA_MAX`), read-only and valid for the call only: a callback that frames it prepends its header into the flat `cmd_in_t` frame passed to `sys_data_provider_send` (SYS_DATA_PROVIDER.MD "Use"). `done` may wait for room on the way back, at most its own bound (the session: its provider's driver).
- NOREPLY, or `done` NULL: no answer of any status. Every failure is reported with its command (`router_report_cmd`: the log line `cmd src f0 seq 7 flags 00 req …`, and the errors stream), answered or not. A `done` that fails is reported and its answer lost: a reader retries.

### Origin, urgent work

- **Origin** `router_origin()`: the command the calling task is routing (src, seq, request), NULL outside the router tasks; inside a replay it is still the outer command. A module that acts for a peer reads its `src` there: the streams' and the remote devices' SUBSCRIBE. `router_is_task()`: the caller runs on a router task (what waits for an answer must not run there).
- **Urgent work** `router_run_urgent(fn)`: runs `fn` once on the urgent task, soon (any task or a timer callback, not an ISR); called again before it ran, it still runs once; another `fn` waiting → `ERR_ROUTER_BUSY`. For work that must not wait behind a command and may block: the safe state of `sys_system` (SYS_SYSTEM.MD). Whether a peer is alive is the session's (SYS_SESSION.MD "Gone peers").

## Errors (`MODULE_ID_ROUTER_QUEUE` = 0x24)

A tag about a command carries its `src`.

| Error | Severity | Context / Fields | Description |
|---|---|---|---|
| `ERR_ROUTER_QUEUE_FULL` | MEDIUM | `u8 src, u8 urgent` | Ring full; the command was not queued |
| `ERR_ROUTER_FRAME_LEN` | MEDIUM | `u8 src, u16 len, u16 max` | A request shorter than 2 B (key, dir) or longer than `CMD_DATA_MAX` |
| `ERR_ROUTER_BUSY` | MEDIUM | – | `router_run_urgent`: another `fn` waits to run |

## Limits

- Command rings: `CONFIG_ROUTER_WORK_QUEUE_SIZE` (default 4 KB) and `CONFIG_ROUTER_URGENT_QUEUE_SIZE` (default 1 KB); each command takes its request, a 12-byte header (callback, `ctx`, origin) and the ring's own header.
- Concurrency: urgent task (priority 15) and work task (priority 5) run concurrently; module routers must be thread-safe.

## Tests

**Host tests** in `host/` (`python test_host/run.py router_queue` from the firmware project, `HOST_TESTS.MD`): the real queue with the real `router` and `errors` on the PC, the two router tasks as threads (the FreeRTOS stand-in: tasks are threads, the queues keep the ring buffer's byte accounting). Two executables:
- `test_router_queue.c`, no task started: `router_report_cmd` (the log line with the command, a long request cut after 32 bytes), what `router_submit` / `router_inject` refuse (NULL, a request outside 2..`CMD_DATA_MAX`) and accept, `router_run_urgent`'s one waiting slot, the work and the urgent queue each filling to `ERR_ROUTER_QUEUE_FULL` independently.
- `test_router_tasks.c`, the tasks running: a command's answer to its `done` with the submitter's ctx, in order; a failure answered as the error record (origin, request, the chain) and reported to the sink with its command; `NOREPLY` (no answer, a failure still reported); an answer of exactly `CMD_DATA_MAX` and one byte more; a `done` that fails reported; no `done`; an urgent command running while the work task is blocked, and a normal one waiting behind it; `router_submit` copying the command and its request; `router_inject` (src `ROUTER_SRC_SELF`, a counting seq, the flags); a command run inside a handler seeing the outer origin; `router_run_urgent` running a fn once on the urgent task, and waking an idle one; a router that registered itself (`ROUTER_MODULE_REGISTER`) bound by `router_init` and answering.

Not covered on the host: priorities (the urgent task is above the work task only on the board), the stack sizes, the scheduler's timing.

Script `router_tests.py` (in `.claude/skills/runit-esp/scripts/`, over a client session on the console: `router_link.py`) covers:
- The sys key (SYS_SYSTEM.MD): PING (uptime), STOP (safe state), DEADMAN (write, read back, the session's timeout, the safe state when the session ends), BYE, TIME (write, read back), RESTART, SAFE / SAFE_LEAVE.
- The direction byte: a write-only command sent as a read, a read-only one as a write, a dir of 2 (`ERR_CMD_DIRECTION`), a frame without it (`ERR_ROUTER_FRAME_LEN`).
- Key dispatching, nested router calls, error chain recording with route keys; the record names this client (`src`) and the seq.
- Urgent task preemption over work queue.
- Frame sizing, queue saturation, and error records; the providers, the sessions and the streams' subscriptions.

