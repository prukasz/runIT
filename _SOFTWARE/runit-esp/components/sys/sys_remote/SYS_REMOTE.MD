# sys_remote — devices of other boards

Component `components/sys/sys_remote/`. A **remote device** is a local id that names a device of another board (SYS_DEVICE.MD "Remote devices"): an alias. A device command is the same on every board, `[0x05][dir][id u16][op u16][args]`, so a call on the alias is that command with the id swapped, sent to its board, which runs it as any command from anyone and does not know it is used remotely. sys_device knows only where the device is and hands its calls to the hook this component sets; the session and the watches carry them. The device's events and errors come back by a **subscription to the device** on its board, made with the alias and ended with it.

Nothing depends on this component, so it may use them all: a board without other boards leaves it out of its boot (`runit` starts it and registers its router when the board has a link, `RUNIT_LINK_ROOT` / `RUNIT_LINK_CHILD`), and its remote devices then answer `ERR_DEV_REMOTE_DOWN`.

```
 the VM ── device block ──► local id 23 → (board 01, id 7) ─┬─ wired:  REQUEST, wait for the ANSWER ───────────────► board 01
                                                            └─ else:   the watch's copy (a watch of the read) ◄───── board 01
 sys_event (as id 23) ◄── the device's events and errors (STREAM, channel 0x81, to its subscribers only) ◄───────── board 01
```

## Files

| File | Role |
|---|---|
| `sys_remote.h` | Use: init, subscribe, unsubscribe, the error sink's hook |
| `sys_remote.c` | The hook (calls), the subscriptions of both sides, the queue and the task |
| `config/sys_remote_config.h` | The kinds of a frame (published to the app) |
| `router/sys_remote_router.h` / `.c` | The remote interface: SUBSCRIBE, UNSUBSCRIBE (root key `ROUTER_KEY_REMOTE` 0x10) |
| `router/contract_remote.json` | The router's descriptor |
| `errors/` | `ERR_SYS_REMOTE_MAP` (`ERR_MOD_SYS_REMOTE` = 21), its registration, the messages |
| `Kconfig` | The read period, the subscriptions, the aliases, the queue, the task |
| `AGENTS.md` | Rules for changing this folder |

## Init

`sys_remote_init()`, a boot step after the session, the watches and the events: sets sys_device's remote hook (`sys_device_set_remote`), which also tells it when a remote device is made or removed (`added`, `removed`: its subscription), listens to the session (gone and reached peers, the channel `SYS_SESSION_CHANNEL_REMOTE`) and starts its task.

## A call

The hook gets the device's record (`board`, `remote_id`, `flags`) and the device command `req` sys_device made of the call, with no lock held but the caller's own. The record's one flag, `SYS_DEVICE_REMOTE_WIRED`, says how a call goes:

| Call | `WIRED` (a neighbour on a wired link) | not `WIRED` |
|---|---|---|
| read | `sys_session_call_wait`, every read: the answer in this pass, never a copy | `sys_watch_read(board, req, 2 × CONFIG_SYS_REMOTE_WATCH_MS, no wait)`: the first read makes an ON_CHANGE watch of it by itself, reading every `CONFIG_SYS_REMOTE_WATCH_MS`; every read then takes its latest value; before the first entry `ERR_WATCH_NO_VALUE`, a copy too old `ERR_WATCH_STALE` (the VM takes both as "later"); a read no longer made loses its watch (SYS_WATCH.MD "Cached reads") |
| write | `sys_session_call_wait`: its error at once | `sys_session_call`, not waited for: its answer comes on the RX task, an ERROR reported as the device's (`ERR_DEV_ID` over `ERR_REMOTE` over that board's chain) |

- After an OK write, in both ways, `sys_watch_stale(board, req, 2)` marks the copies of that device old and reads each again at once: a read after a write does not take a value from before it.
- `WIRED` with the way not wired now (`sys_session_wired`): the call goes the other way, and that is reported once per board until the way is wired again, `ERR_REMOTE_NOT_WIRED{board}` by `err_report_board`: the program's timing has changed.
- A wired call waits at most the session's call timeout: the VM's pass is longer by the round trip, the price of the answer in the same pass.
- `ERR_SESSION_BUSY` goes back as it is: "later", which the VM's device blocks take as such. `ERR_SESSION_NO_ROUTE` or `_PEER_DOWN`: its board is gone, `ERR_DEV_REMOTE_DOWN{device, board}`. No state is kept for it: the next call tries again, and works once that board is reached again.
- A command's errors come in its answer only. An error of a command from someone else (the app driving that device) stays with that one and that board's policy; it reaches no subscriber.
- The args of a call are at most `SYS_DEVICE_REMOTE_ARGS_MAX` (64) bytes.
- **Who may use whom**: a master a slave's device, a slave its master's. A slave another slave's: no route, the master never forwards between slaves (`ERR_DEV_REMOTE_DOWN`).
- **OPS** of a remote device: the app asks the device's own board, which alone knows its wiring (`has_op`).

## Subscriptions

**On the device's board**: SUBSCRIBE `[id u16]` adds (the command's `src`, the device's id) to one table, `CONFIG_SYS_REMOTE_SUBSCRIPTIONS`, each with its own `seq u16`. A peer that is no peer (`SELF`, `NONE`, `BROADCAST`): `ERR_CMD_BAD_ARG`; the table full: `ERR_REMOTE_SUBS_FULL{id, max}`; whether the device exists is not checked (an id with no device sends nothing). The session's gone ends a peer's subscriptions.

- **An event**: the first subscription of a device subscribes a function to its events (`sys_event_subscribe_fn`, its source the device; the last one gone drops it); it runs on the event task.
- **An error**: runit's error sink calls `sys_remote_on_error(chain, cmd)` for every reported chain: one with no command (`cmd` NULL) whose chain has an `ERR_DEV_ID` in the table is made an error record (`err_record`). `sys_device_write` wraps the drivers' timers' errors in `ERR_DEV_ID` too.
- Both only queue: a reporter may be a timer's task, and `sys_session_stream` takes a whole frame on the stack. The task sends a STREAM `[0x81][id u16][kind][data]` to each subscriber of the device, `kind` `SYS_REMOTE_EVENT` (`sys_event_t`, its source the device's id here) or `SYS_REMOTE_ERROR` (an error record; urgent), `seq` the subscription's.
- **Lost frames**: a frame that no queue took (this one full, or the way's `NOWAIT` refused) still counts in `seq`.

**On the alias's board**: a remote device made (`added`) takes a slot of `CONFIG_SYS_REMOTE_ALIASES` (board, remote id, local id, the last `seq`) and sends SUBSCRIBE to its board (a call, never waited for); removed, UNSUBSCRIBE. A board reached again gets the SUBSCRIBE of each of its aliases again; a board gone starts their `seq` again.

- A frame of the channel `0x81` (the session's stream listener, on the RX task) is queued and taken on the task: by its board and id to the alias, the gap in its `seq` reported once per gap as `ERR_REMOTE_LOST{board, count}` (`err_report_board`, so runit's policy per board answers it); an event published as the local id's (`sys_event_publish_deferred`, so ON_EVENT and this board's listeners see it), an error reported as the local id's (`ERR_DEV_ID` over `ERR_REMOTE{board, seq}` over that board's chain: `err_record_parse`), so its policy answers it.
- SUBSCRIBE answered ERROR (the device's board's table full): reported about the board (`ERR_REMOTE` over that chain, `err_report_board`); sent again when the board is reached again. No alias slot: `ERR_REMOTE_ALIASES_FULL{board, max}` about the board; the device's calls work, its events and errors do not come.

## Remote (`sys_remote_router`, root key `ROUTER_KEY_REMOTE` 0x10)

| Code | Command | Side | Request / answer |
|---|---|---|---|
| `0x01` | SUBSCRIBE | write | `u16 id`, a device of this board; the subscriber: the command's `src` |
| `0x02` | UNSUBSCRIBE | write | `u16 id`, of the command's `src` |

## Errors (`ERR_MOD_SYS_REMOTE` = 21)

`ERR_REMOTE_NOT_WIRED{board}`, `_LOST{board, count}`, `_SUBS_FULL{id, max}`, `_ALIASES_FULL{board, max}`. Code 1 is unused.

## Limits

| Option | Default | Meaning |
|---|---|---|
| `CONFIG_SYS_REMOTE_WATCH_MS` | 100 | a not wired device's read: its watch reads this often, a copy is good for twice this |
| `CONFIG_SYS_REMOTE_SUBSCRIPTIONS` | 16 | subscriptions of other peers to this board's devices |
| `CONFIG_SYS_REMOTE_ALIASES` | 16 | remote devices of this board whose events and errors come here |
| `CONFIG_SYS_REMOTE_QUEUE_SIZE` | 1024 | bytes of the queue: frames out to the subscribers and in from the boards |
| `CONFIG_SYS_REMOTE_TASK_PRIO` / `_STACK` | 4 / 4096 | its task |

## Needs elsewhere

- **sys_device**: the remote record, the hook, `sys_device_find_remote`, the remote devices made and removed (SYS_DEVICE.MD "Remote devices").
- **sys_session**: calls, the wait, `sys_session_wired`, `ERR_SESSION_BUSY`, `sys_session_stream` in `SYS_SESSION_CHANNEL_REMOTE`, the gone, reached and stream listeners (SYS_SESSION.MD).
- **sys_watch**: `sys_watch_read`, `sys_watch_stale` (SYS_WATCH.MD "Cached reads").
- **sys_event**: `sys_event_subscribe_fn`, `sys_event_publish_deferred`.
- **errors**: `err_record`, `err_record_parse`, `err_report_board`.
- **runit**: its error sink calls `sys_remote_on_error`; its policy per board answers the errors about a board.

## Open

- The policy per board has no wire command: the board config sets it in C.
- An error of a write that was not waited for reaches the program only as the device's report (its policy), not in the block.
- A failed CHANGE of the VM's device block counting as not sent (needs that error in the block).
