# sys_device_remote — devices of other boards

Component `components/sys/sys_device_remote/`. A **remote device** is a local id that names a device of another board (SYS_DEVICE.MD "Interceptors and remote devices"): an alias. A device command is the same on every board, `[MODULE_ID_SYS_DEVICE][dir][id u8][op u16][args]`, so a call on the alias is that command with the id swapped, sent to its board, which runs it as any command from anyone and does not know it is used remotely. sys_device knows only where the device is and hands its calls to the hook this component sets; the session and the watches carry them. The device's events and errors come back by a **subscription to the device** on its board, made when the alias is created and ended with it.

Nothing depends on this component, so it may use them all. `app/runit` starts it and registers its router on every board (one build); a board with no other board on its ways has nothing to call, and its remote devices answer `ERR_DEV_REMOTE_DOWN`.

```
 the VM ── device block ──► local id 23 → (board 01, id 7) ─┬─ WIRED:    REQUEST, wait for the ANSWER ───────────────► board 01
                                                            └─ WIRELESS: the watch's copy (a watch of the read) ◄───── board 01
 sys_event (as id 23) ◄── the device's events and errors (PUSH out of `REMOTE`, to its subscribers only)   ◄───────── board 01
```

## Files

| File | Role |
|---|---|
| `sys_device_remote.h` | Use: init, subscribe, unsubscribe, the error sink's hook |
| `sys_device_remote.c` | The hook (calls), the subscriptions of both sides, the queue and the task |
| `config/sys_device_remote_config.h` | The kinds of a frame (published to the app) |
| `router/sys_device_remote_router.h` / `.c` | The remote interface: SUBSCRIBE, UNSUBSCRIBE (`MODULE_ID_REMOTE` 0x2F). The header has the op table (`REMOTE_OPERATIONS`) and the frame (`sys_device_remote_ref_t`); the router registers itself |
| `router/contract_remote.json` | The router's descriptor |
| `errors/` | `ERR_SYS_DEVICE_REMOTE_MAP` (`MODULE_ID_REMOTE` 0x2F), the messages |
| `Kconfig` | The subscriptions, the queue, the task |
| `AGENTS.md` | Rules for changing this folder |

## Init

`sys_device_remote_init()`, a boot step after the session, the watches and the events: sets sys_device's remote hook (`sys_device_set_remote`), subscribes to the registry's `CREATED` event (a remote device made: its subscription), listens to the session (`SESSION_EV_GONE` and `_REACHED`), and registers its port `REMOTE`: what comes in is linked to it from the sessions' sub-ports `MODULE_ID_REMOTE` (`app/runit` gives the board's own sessions one, IN; a session of the board's record its own), what goes out goes to them by name. Its task starts by itself (a constructor, before `app_main`) and waits on the queue.

## A call

The hook gets the device's record (`board`, `remote_id`, `flags`), the direction, the op, its flags (`SYS_DEVICE_OP_*` of its row) and its args (a whole request at most: `CMD_DATA_MAX + 1` with the 5 B of the device command's head, else `ERR_CMD_BAD_LEN`), with no lock held but the caller's own. The registry has checked the op against the device's type. The record's flag says how a call goes:

| Call | `WIRED` (a neighbour on a wired link) | `WIRELESS` |
|---|---|---|
| read | `router_request_wait`, every read: the answer in this pass, never a copy | a `DIRECT` read (it takes something or costs: UART RECEIVE, I2C PROBE / SCAN / RAW) as WIRED: sent and waited for, never from a watch, which would run it every period. Any other: `sys_watch_read(board, req, ...)`: the first read makes an ON_CHANGE watch of it by itself; every read then takes its latest value; before the first entry `ERR_WATCH_NO_VALUE`, a copy too old `ERR_WATCH_STALE` (the VM takes both as "later") (SYS_WATCH.MD "Cached reads") |
| write | `router_request_wait`: its error at once | `router_request`, not waited for: its answer comes on the RX task, an ERROR reported as the device's (`ERR_DEV_ID` over `ERR_REMOTE` over that board's chain) |

- After an OK write, in both ways, `sys_watch_stale(board, req, 1)` marks the copies of that device old and reads each again at once: a read after a write does not take a value from before it.
- `WIRED` with the way not wired now (`sys_session_wired`): the call goes the other way, and that is reported once per board until the way is wired again, `ERR_REMOTE_NOT_WIRED{board}` by `err_report_board`: the program's timing has changed.
- A wired call waits at most the session's call timeout: the VM's pass is longer by the round trip, the price of the answer in the same pass.
- `ERR_SESSION_NO_ROUTE` (no open session to the board) or `ERR_ROUTER_CALL_DOWN` (its session ended during the call): its board is gone, `ERR_DEV_REMOTE_DOWN{id, board}`. No state is kept for it: the next call tries again, and works once that board is reached again. Any other error (the call's timeout, the board's own answer, `ERR_WATCH_NO_VALUE` / `_STALE`) goes back as it is.
- A command's errors come in its answer only. An error of a command from someone else (the app driving that device) stays with that one and that board's policy; it reaches no subscriber.
- **Who may use whom**: a board a device of a board it has a session with (`ERR_DEV_REMOTE_DOWN` when it has none): no relay, a session is between two.
- **OPS** of a remote device: the app asks the device's own board, which alone knows its wiring.

## Subscriptions

**On the device's board**: SUBSCRIBE `[id u8]` adds (the command's `src`, the device's id) to one table, `CONFIG_SYS_DEVICE_REMOTE_SUBSCRIPTIONS`. A peer that is no peer (`SELF`, `NONE`, `ALL`): `ERR_CMD_BAD_ARG`; a peer whose session has no sub-port `REMOTE` (the frames could not reach it): `ERR_SESSION_NO_ROUTE{dst}`; the table full: `ERR_REMOTE_SUBS_FULL{id, max}`; whether the device exists is not checked (an id with no device sends nothing). The session's gone ends a peer's subscriptions.

- **An event**: the first subscription of a device subscribes a function to all its events (`sys_event_subscribe`, `SYS_EVENT_RUN_QUEUED_DEVICE`: all of a device's events are in the DEVICE lane; its source the device; the last one gone drops it). The registry's `CREATED` and `REMOVED` are not sent: on the other board they would reach the policy as the alias's own, and it would forget the alias's settings (the alias sees the device gone as `ERR_DEV_NOT_FOUND` in the answer); `SUSPENDED`, `RESUMED` and `RESET` go.
- **An error**: the app's error sink calls `sys_device_remote_on_error(chain, cmd)` for every reported chain: one with no command (`cmd` NULL) whose chain has an `ERR_DEV_ID` in the table is made an error record (`err_record`). `sys_device_write` wraps the drivers' timers' errors in `ERR_DEV_ID` too.
- Both only queue: a reporter may be a timer's task. The task sends a frame `[hdr PUSH, MODULE_ID_REMOTE][id u8][kind][data]` (queued with the room of its header, written there: no copy) out of the port `REMOTE` (`DATA_MATRIX_PORT(MODULE, MODULE_ID_REMOTE)`, 0x2F) to the sub-port `MODULE_ID_REMOTE` of each subscriber of the device, `kind` `SYS_DEVICE_REMOTE_EVENT` (`sys_event_t` with its payload, its source the device's id here) or `SYS_DEVICE_REMOTE_ERROR` (an error record; urgent). A frame the queue or the way did not take is lost: it shows as a gap in the session's PUSH `seq` (SYS_SESSION.MD "Counting push frames"), which the receiver does not count.

**On the alias's board**: the registry's `CREATED` of a remote device (`sys_device_remote_of`) sends SUBSCRIBE to its board (a call, never waited for). A board reached again gets the SUBSCRIBE of each of its remote devices again (a loop over the registry's remote devices). There is no table of aliases: `(board, remote id) → local id` is the registry's (`sys_device_find_remote`).

- A frame on `REMOTE`'s input (from a session's sub-port linked to it, on the RX task; the board is its `src`, a frame from anyone but a board is dropped) is queued and taken on the task: by its board and id to the local id; the event is published as the local id's (`sys_event_publish` with `SYS_EVENT_DEFER`, so ON_EVENT and this board's listeners see it), the error reported as the local id's (`ERR_DEV_ID` over `ERR_REMOTE{board, 0}` over that board's chain: `err_record_parse`), so its policy answers it, and stops at `ERR_REMOTE` (SYS_DEVICE_POLICY.MD).
- A frame for a device no remote device names (it was removed) is answered by UNSUBSCRIBE to its sender: the subscription of a removed alias ends at its first event.
- SUBSCRIBE with no answer in the call's time (`ERR_ROUTER_CALL_TIMEOUT`: lost on the way) is sent again at once; its session gone meanwhile (`ERR_ROUTER_CALL_DOWN`): the board's REACHED sends it again. Refused by the board (its table full, no sub-port `REMOTE` on its session to this one): its answer, already under `ERR_REMOTE`, is reported about the board (`err_report_board`); sent again when the board is reached again.

## Remote (`sys_device_remote_router`, root key `MODULE_ID_REMOTE` 0x2F)

The ops and the frame are in `sys_device_remote_router.h`; `contract_remote.json` names them.

| Code | Command | Side | Request / answer |
|---|---|---|---|
| `0x01` | SUBSCRIBE | write | `sys_device_remote_ref_t` `{u8 id}`, a device of this board; the subscriber: the command's `src` |
| `0x02` | UNSUBSCRIBE | write | `sys_device_remote_ref_t`, of the command's `src` |

## Errors (`MODULE_ID_REMOTE` 0x2F)

`ERR_REMOTE_NOT_WIRED{board}`, `_SUBS_FULL{id, max}`.

## Limits

| Option | Default | Meaning |
|---|---|---|
| `CONFIG_SYS_DEVICE_REMOTE_SUBSCRIPTIONS` | 16 | subscriptions of other peers to this board's devices |
| `CONFIG_SYS_DEVICE_REMOTE_QUEUE_SIZE` | 1024 | bytes of the queue: frames out to the subscribers and in from the boards |
| `CONFIG_SYS_DEVICE_REMOTE_TASK_PRIO` / `_STACK` | 4 / 4096 | its task |

## Needs elsewhere

- **sys_device**: the remote record, the hook, `sys_device_find_remote`, `sys_device_remote_of`, the `CREATED` event (SYS_DEVICE.MD).
- **sys_device_policy**: its walk of a chain ends at `ERR_REMOTE`.
- **router_queue**: the calls and the wait (`router_request`, ROUTER_QUEUE.MD "Calls"); **sys_session**: `sys_session_port` (the port of a board's commands), `sys_session_wired`, `sys_session_peer`, the gone and reached events (SYS_SESSION.MD); **data_matrix**: its port `REMOTE`, in and out (DATA_MATRIX.MD).
- **sys_watch**: `sys_watch_read`, `sys_watch_stale` (SYS_WATCH.MD "Cached reads").
- **sys_event**: `sys_event_subscribe`, `sys_event_publish`.
- **errors**: `err_record`, `err_record_parse`, `err_report_board`.
- **app/runit**: its error sink calls `sys_device_remote_on_error`.

## Open

- A board's safe state does not reach the master as an event: the sweep's `SUSPENDED` / `RESUMED` (source `SYS_DEVICE_ID_NONE`) is not sent, the master learns of it from the first refused write.
- The queue (1 KB) smooths a series of events; whether it is needed is for the measurement of lost frames on BLE.
- The policy per board has no wire command: the board config sets it in C.
- An error of a write that was not waited for reaches the program only as the device's report (its policy), not in the block.
- No host test yet (the bridge on a fake session: the frames, the UNSUBSCRIBE of a removed alias).
