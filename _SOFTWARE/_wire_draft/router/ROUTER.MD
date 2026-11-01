# router — the commands of this board (draft)

Component `components/core/router/` (draft in `_wire_draft/`, not built). The router runs the commands of this board: it queues them, routes them through the root table and the module's table on one of its two tasks, and hands every answer to the callback its command came with. It knows **commands, not frames**: the session makes a command out of a frame and a frame out of an answer, so the header and the way back are the session's.

```
 who sends                          router                                     module / device
 ─────────                          ──────                                     ───────────────
 session: frame → err_cmd_t ─┐
          router_submit      ├──►  urgent ring ─► urgent task ─┐
 board:   router_inject ─────┘     work ring ───► work task ────┼─ route ──►  cmd_f: answers now,
                                                                │                    waits on nothing but its own I/O
 done(ctx, cmd, status, data) ◄──  OK / ERROR  ◄────────────────┘
   the session frames it to src; a watch keeps it; a call of the board to itself takes it
```

## Files

| File | Role |
|---|---|
| `router.h` | Flags and statuses, command tables and op macros, register, route, init, submit, inject, tap, origin, is-task, urgent work |
| `router_ids.h` | The root keys (`ROUTER_KEY_*`), as today |
| `errors/router_errors.h` | `ERR_ROUTER_MAP` |
| `Kconfig` | Keys, queues, tasks |

## A command

`err_cmd_t` (errors.h, so an error record names it): the request (`[key][dir][code | id + op][args]`) and its origin:

| Field | Meaning |
|---|---|
| `src` | Who sent it: a board or client id (the session fills it from its frame), or `ROUTER_SRC_SELF` (the board itself, `router_inject`) |
| `seq` | The sender's counter, `u16`; in every answer |
| `flags` | `ROUTER_FLAG_URGENT` (the urgent task), `ROUTER_FLAG_NOREPLY` (no answer; a failure still goes to the errors stream). A board's call to another board is always answered, so NOREPLY goes between boards only on a broadcast (SYS_SESSION.MD "Answers") |

`router_submit(&cmd, done, ctx)` copies the command, its callback and `ctx` into the ring of its flags and returns; it never blocks, so the session calls it on the driver's RX task. `router_inject(req, flags, done, ctx)` is the same for the board's own commands (src `ROUTER_SRC_SELF`, seq from the router's counter): the actions, the project's start, a watch's read, a call of the board to itself. A command that can't be queued (bad length, a full ring) is not: the call returns why, and the caller answers it (the session: to its source, SYS_SESSION.MD "A request for this board").

The router keeps nothing of who sent a command: the callback and its `ctx` do. So there is no registry of links and no state per peer.

## Routing

As today: `router_call` on the root table (`[key]…`), each module's `router_call_op` on its command table (`[dir][code][args]`), every level wrapping a failure in `ERR_ROUTER_KEY`, the op macros parsing the payload. What changes is the handler: `cmd_f(ctx, cmd_in_t in, cmd_out_t* out)` (CMD.MD), the macros pass `in` and `out` whole, and `out->cap` is `CMD_DATA_MAX` for every command (CMD.MD "Frame size").

A handler waits on nothing but its own I/O (CMD.MD "Long work"): what waits on the world is a job, answered at once. `router_route` is also the replay of stored frames (actions, project), on the calling task.

## The tasks

| Task | Prio | Takes | Also |
|---|---|---|---|
| urgent | 15 | the urgent ring | `router_run_urgent(fn)` (the safe state) |
| work | 5 | the work ring, one command at a time | |

Each task runs a command and then its callback with the answer, on the same task: the answer is in the task's own buffer, so nothing is copied for it.

## Answers

The router calls the command's `done(ctx, cmd, status, data, len)`:

| Status | When | Data |
|---|---|---|
| `OK` | the handler returned NULL | the answer (`out->len` bytes, maybe none) |
| `ERROR` | the handler returned a chain | the error record (`err_record(cmd, chain)`) |

- One command, one answer, at most `CMD_DATA_MAX`: longer data is paged by its command (CMD.MD "The handler").
- NOREPLY, or `done` NULL: no answer of any status.
- Every failure is reported with its command (log and errors stream), answered or not.
- `done` may wait for room on the way back, at most its own bound (the session: its provider's driver). A `done` that fails is reported and its answer lost; a reader retries, a writer reads back (SYS_SESSION.MD "Retries").

## Tap, origin, urgent work

As today, on `err_cmd_t`: the tap (`take` before routing, `seen` after; the actions recorder), `router_origin()` (the command of the calling task, valid until its handler returns), `router_is_task()` (the caller is on a router task: what must not wait for an answer asks it). A module that acts for a peer reads its `src` there: the connectors' SUBSCRIBE, the watches' ADD. `router_run_urgent(fn)` runs `fn` once on the urgent task, soon (any task or a timer callback): the safe state, which must not wait behind a command. Whether a peer is alive is the session's (SYS_SESSION.MD "Gone peers").

The flags `ROUTER_FLAG_URGENT` and `_NOREPLY` are defined here only: the session's frame byte carries them at the same bits, and `sys_session_call` takes them.

## Errors (`ERR_MOD_ROUTER` = 2)

Kept: `ERR_ROUTER_KEY`, `_NO_KEY`, `_UNKNOWN_KEY`, `_KEY_TAKEN`, `_TABLE_FULL`; `_QUEUE_FULL{src, urgent}` and `_FRAME_LEN{src, len, max}` name the sender, not a link. Codes 4, 5 and 8 are unused. New:

| Error | Severity | Fields | When |
|---|---|---|---|
| `ERR_ROUTER_BUSY` | MEDIUM | – | `router_run_urgent`: another `fn` waits to run |

## Limits

| Option | Default | Meaning |
|---|---|---|
| `CONFIG_ROUTER_MAX_KEYS` | 16 | root keys |
| `CONFIG_ROUTER_WORK_QUEUE_SIZE` / `_URGENT_QUEUE_SIZE` | 4096 / 1024 | the rings of commands |
| `CONFIG_ROUTER_*_TASK_PRIO` / `_STACK` | 5, 15 / 6144 | the tasks |

The frame size is `CMD_FRAME_MAX` (CMD.MD); the router has no frame limit of its own.

## Needs elsewhere

- **errors.h**: `err_cmd_t` is `{req, req_len, seq u16, src u8, flags u8}`; the error record's command header follows it (`_wire_draft/payloads.h`).
- **The session** (SYS_SESSION.MD): it makes `err_cmd_t` from a request frame and submits it with its framer as `done`, answers a command the router refused, and forwards the frames for other boards without the router.
- **sys_actions, sys_project**: `router_inject` with flags and no `done`; the actions recorder names a client by the origin's `src`.
- **sys_system**: the dead-man is the session's (SYS_SESSION.MD "Gone peers"); `router_run_urgent` runs the safe state.
- **sys_uart, sys_ble**: no router link; each is a provider's driver (SYS_DATA_PROVIDER.MD).

## Open

- The `.c` files: the rings and tasks, the inject counter.
