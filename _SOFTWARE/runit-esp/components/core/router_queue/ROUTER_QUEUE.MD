# router_queue — the commands of this board in a queue (for agents)

Component `components/core/router_queue/`. It runs the commands of this board: it queues them, routes them (`router_route`, ROUTER.MD) on one of its two tasks, and hands every answer back. A command comes from the router's **port** in the data matrix (`ROUTER_PORT`, DATA_MATRIX.MD), as a frame with its header (CMD.MD "The header"), and its answer goes back as a frame to the port it came from; or from this board's C (`router_inject`), and its answer goes to the **callback** it came with. This board's own **calls** (`router_request`) leave from the same port and their answers come back to it.

```
 who sends                                router                                     module / device
 ─────────                                ──────                                     ───────────────
 a port: [hdr REQUEST][body] ──► port ─┐
 C:      router_inject ────────────────┼─►  urgent ring ─► urgent task ─┐
                                       │    work ring ───► work task ────┼─ route ──►  cmd_f: answers now
                                       │                                 │
 a port ◄── [hdr ANSWER][data] ────────┴── OK / ERROR ◄─────────────────┘   (or done(ctx, cmd, status, data))
 router_request ──► port ──► [hdr REQUEST] to a linked port; its [hdr ANSWER] ──► the call's callback
```

## Files

| File | Role |
|---|---|
| `router_queue.h` | The flags and statuses (of a command, `cmd_req_t` in `utils/cmd_types.h`; its error record is `err_record_cmd`, `err_record.h`), `ROUTER_PORT`, `router_init`, submit, inject, the calls (`router_request`, `_wait`, `_fail_to`), report, origin, is-task, urgent work. The routing, the handler and the op tables are `router.h` (ROUTER.MD) |
| `errors/router_queue_errors.h`, `errors/errors_router_queue.json` | `ERR_ROUTER_QUEUE_MAP` (`MODULE_ID_ROUTER_QUEUE`) and the messages |
| `router_queue.c` | `router_inject`, the port (its frames in, the answers out to ports), the urgent and work tasks (started by a constructor, before `app_main`), the answers to the callbacks, the report of a failed command, origin, `router_run_urgent`, `router_init` |
| `router_request.c` | This board's calls: their slots, the answers that end them, their timer, the wait |
| `internal/router_queue_internal.h` | Between the two: the port, an answer for a call, the calls' start |
| `Kconfig` | Queues, task prio and stack |
| `AGENTS.md` | Rules for changing this folder |

## A command

A command is a `cmd_req_t` (`cmd_types.h` in `utils`; an error record names it): the request `[key][dir][code | id + op][args]` and its origin:

| Field | Meaning |
|---|---|
| `src` | Who sent it (`CMD_SRC_*`, cmd.h): a board or client id (the frame's header), `CMD_SRC_SELF` (0xFE: the board itself, `router_inject`), or `CMD_SRC_NONE` (0x00: nobody said) |
| `seq` | The sender's counter, `u16`; in its answer |
| `flags` | `ROUTER_FLAG_URGENT` (bit 0: the urgent task; the client decides what is urgent, the firmware has no fixed list), `ROUTER_FLAG_NOREPLY` (bit 1: no answer; a failure still goes to the errors stream) |

- `dir` (`cmd_dir_e`, `router_queue.h`): 0 `CMD_DIR_READ`, 1 `CMD_DIR_WRITE`, the side of the command that runs (ROUTER.MD "Read and write"). A module's command is `[key][dir][code][args]`; a device's `[MODULE_ID_SYS_DEVICE][dir][id u8][op u16][args]` (SYS_DEVICE.MD). A side the command doesn't have, or a dir above 1: `ERR_CMD_DIRECTION{dir}`.
- Payloads are packed little-endian structs, size checked exactly (`CMD_HANDLER`, ROUTER.MD "Routing").
- The flags are defined here only: the header's type byte carries them at the same bits (`CMD_TYPE_FLAGS`, cmd.h; checked at build time in `router_queue.h`), and an answer's status at bit 2 (`CMD_TYPE_STATUS`).

## Commands and answers

- `router_inject(req, flags, done, ctx)` runs a command of the board itself: it copies the request, its callback and `ctx` into the ring of its flags (src `CMD_SRC_SELF`, seq from the router's counter) and returns; it never blocks. A command it can't queue (a request shorter than 2 B or longer than `CMD_DATA_MAX` + 1: `ERR_ROUTER_FRAME_LEN`; ring full: `ERR_ROUTER_QUEUE_FULL`) is not: the call returns why. The actions (`sys_actions_request`), the board's profile (`sys_board`) and a watch's read inject.
- **From the port**: a REQUEST frame is queued as a command (`src`, `seq` and the flags of its header; its request is the header's `key` and the body) with the port it came from (`from`); its answer leaves the router's port to that port by its name (`data_matrix_send_to`, no link back needed) as `[hdr ANSWER: status, URGENT, src 0, the request's key and seq][data]`, waiting for room at most the provider's bound; what is not taken is lost. A frame the queue refuses is answered ERROR at once (`NOWAIT`, not a NOREPLY one) on the sender's task, its record at most `ERR_RECORD_MAX`, and reported. An ANSWER frame ends this board's call (Calls); another kind is dropped.
- The router keeps nothing of who sent a command: the port it came from, or the callback and its `ctx`, do. So there is no registry of links and no state per peer: whatever is linked with the port may send commands, and the answer goes back to it.
- **The answer**: each task runs a command and then its `done(ctx, cmd, status, data, len)` on the same task:

| Status | When | Data |
|---|---|---|
| `OK` | the handler returned NULL | the answer (`out->len` bytes, maybe none) |
| `ERROR` | the handler returned a chain | the error record (`err_record_cmd(cmd, chain)`, ERRORS.MD "Error record") |

- `data` is in the task's own buffer (`CMD_FRAME_MAX`: the answer's header, then `CMD_DATA_MAX` of data), read-only and valid for the call only; an answer to a port gets its header in front in that buffer, no copy.
- NOREPLY, or `done` NULL: no answer of any status. Every failure is reported with its command (the log line `cmd src f0 seq 7 flags 00 req …`, and the errors stream: `err_report_ex`), answered or not. A `done` that fails is reported and its answer lost: a reader retries.

## Calls

`router_request(to, req, flags, timeout_ms, cb, ctx)` runs a command and gives its answer to `cb`, a `router_done_f` (read-only data): one way to run a command with its answer, at the other end of a link. A command of this board is `router_inject`. `req` is `[key][dir][code | id + op][args]`: its key goes into the header.

- `to` is the port the request goes to, by its name (`data_matrix_send_to`, no link): a session's command sub-port (`sys_session_port`). A call slot (`CONFIG_ROUTER_CALLS`) keeps it under a new `seq` and `[hdr REQUEST, src 0, key, seq][the rest]` goes out to `to` (`NOWAIT`); `to` not registered: `ERR_ROUTER_NO_PORT`. A call ends once: whoever ends it (its answer, the timer, `router_request_fail_to`, a waiter that gave up) takes it out of its slot under the calls' lock first, and only then is the callback called; a late answer finds no call and is dropped. An answer is the call's when its `seq` and the port it came from are.

| Answer | `cb` gets |
|---|---|
| OK | the data |
| ERROR | that board's error record with `ERR_REMOTE{board, seq}` (the answer's `src`) put before its chain, so `err_record_parse` gives this board's chain and the error policy's walk can stop at it |
| none in its timeout | ERROR: this board's record of `ERR_ROUTER_CALL_TIMEOUT{port, seq}` |
| `router_request_fail_to(to)` (the session behind `to` ended) | ERROR: this board's record of `ERR_ROUTER_CALL_DOWN{port}` |

`cb`'s command is the call: its request (the first `CONFIG_ERRORS_RECORD_REQ_MAX` bytes), its `seq`, `src` `CMD_SRC_SELF`. **Every call out is answered.** **Timeout**: `timeout_ms`, or `CONFIG_ROUTER_CALL_TIMEOUT_MS` (1 s) for 0, checked by a board timer every 10 ms (its callbacks on the board timers' task). A source retries a request after its timeout only when it is a read; a write without an answer is reported to whoever asked, who reads the state back.

`router_request_wait(to, req, flags, timeout_ms, buf, cap, &len)` is the same call, waited for: the answer in `buf`, or the error as a chain (`ERR_REMOTE` over that board's chain, `ERR_ROUTER_CALL_TIMEOUT`, `_CALL_DOWN`; `ERR_CMD_BAD_LEN` for an answer longer than `cap`).

`router_remote_chain(record, len, board, seq)` makes another board's error record a chain under `ERR_REMOTE{board, seq}`: the one way to do it, for an ERROR answer (here), a watch's entry (SYS_WATCH.MD) and a remote device's frame (SYS_DEVICE_REMOTE.MD).

- The wait ends on a flag the call's callback sets, not on the task's notification: another notification of the waiting task wakes it, finds the flag down and sleeps again, so the wait never ends early and the callback never writes into a frame that is gone.
- **Its own limit**: the waiter sleeps at most the call's timeout and a margin (50 ms). Past it, it takes the call out of its slot itself and returns `ERR_ROUTER_CALL_TIMEOUT`; if another task is ending it at that moment, it waits for that end. So a wait never hangs, also on a task the answer or the timer would need (the board timers' task; the RX task of the link).
- A router task may wait for another board: the commands behind it wait that long too.

A board is called by its id through `sys_session_port(board, MODULE_ID_ROUTER, &port)` (SYS_SESSION.MD "The board's own calls").

### Origin, urgent work

- **Origin** `router_origin()`: the command the calling task is routing (src, seq, request), NULL outside the router tasks; inside a replay it is still the outer command. A module that acts for a peer reads its `src` there: the streams' and the remote devices' SUBSCRIBE. `router_is_task()`: the caller runs on a router task (what waits for an answer must not run there).
- **Urgent work** `router_run_urgent(fn)`: runs `fn` once on the urgent task, soon (any task or a timer callback, not an ISR); called again before it ran, it still runs once; another `fn` waiting → `ERR_ROUTER_BUSY`. For work that must not wait behind a command and may block: the safe state of `sys_system` (SYS_SYSTEM.MD). Whether a peer is alive is the session's (SYS_SESSION.MD "Gone peers").

## Errors (`MODULE_ID_ROUTER_QUEUE`)

A tag about a command carries its `src`.

| Error | Severity | Context / Fields | Description |
|---|---|---|---|
| `ERR_ROUTER_QUEUE_FULL` | MEDIUM | `u8 src, u8 urgent` | Ring full; the command was not queued |
| `ERR_ROUTER_FRAME_LEN` | MEDIUM | `u8 src, u16 len, u16 max` | A request shorter than 2 B (key, dir) or longer than `CMD_DATA_MAX` + 1 (a frame's body and its key) |
| `ERR_ROUTER_BUSY` | MEDIUM | – | `router_run_urgent`: another `fn` waits to run |
| `ERR_ROUTER_CALLS_FULL` | MEDIUM | – | Every `CONFIG_ROUTER_CALLS` slot in use |
| `ERR_ROUTER_NO_PORT` | MEDIUM | `u16 port` | A call to a port that is not registered |
| `ERR_ROUTER_CALL_TIMEOUT` | MEDIUM | `u16 port, u16 seq` | A call without an answer in its time |
| `ERR_ROUTER_CALL_DOWN` | MEDIUM | `u16 port` | The link the call went out on ended (`router_request_fail_to`) |
| `ERR_REMOTE` | NONE | `u8 board, u16 seq` | Context: the chain under it is that board's |

## Limits

- Command rings: `CONFIG_ROUTER_WORK_QUEUE_SIZE` (default 4 KB) and `CONFIG_ROUTER_URGENT_QUEUE_SIZE` (default 1 KB); each command takes its request, a 16-byte header (callback, `ctx`, origin, the port it came from) and the ring's own header.
- Calls: `CONFIG_ROUTER_CALLS` (16) slots of about 52 B; `CONFIG_ROUTER_CALL_TIMEOUT_MS` (1000).
- Concurrency: urgent task (priority 15) and work task (priority 5) run concurrently; module routers must be thread-safe.

## Tests

Script `router_tests.py` (in `.claude/skills/runit-esp/scripts/`, over a client session on the console: `router_link.py`) covers:
- The sys key (SYS_SYSTEM.MD): PING (uptime), STOP (safe state), DEADMAN (write, read back, the session's timeout, the safe state when the session ends), BYE, TIME (write, read back), RESTART, SAFE / SAFE_LEAVE.
- The direction byte: a write-only command sent as a read, a read-only one as a write, a dir of 2 (`ERR_CMD_DIRECTION`), a frame without it (`ERR_ROUTER_FRAME_LEN`).
- Key dispatching, nested router calls, error chain recording with route keys; the record names this client (`src`) and the seq.
- Urgent task preemption over work queue.
- Frame sizing, queue saturation, and error records; the providers, the sessions and the streams' subscriptions.


## Open

