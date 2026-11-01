# sys_remote — devices of other boards

Component `components/sys/sys_remote/`. It carries the remote devices of this board (SYS_DEVICE.MD "Remote devices") to their boards and back: sys_device knows only where a device is and hands its calls to the hook this component sets; the session, the watches and the mirrors do the carrying. Nothing depends on it, so it may use them all: a board without other boards leaves it out of its boot (`runit` starts it when the board has a link, `RUNIT_LINK_ROOT` / `RUNIT_LINK_CHILD`), and its remote devices then answer `ERR_DEV_REMOTE_DOWN`.

```
 sys_device_call (a remote device) ── hook ──► sys_remote ─┬─ write: sys_session_call / _call_wait, then sys_watch_stale
                                                           └─ read:  sys_watch_read (a copy, a watch, or a request now)
 that board's EVENTS_ALL ─► mirror ─► sys_remote: source → the local id ─► sys_event_publish_deferred ─► ON_EVENT, listeners
 that board's ERRORS     ─► mirror ─► sys_remote: request → the local id ─► err_report as the device's ─► its policy
```

## Files

| File | Role |
|---|---|
| `sys_remote.h` | Use: init |
| `sys_remote.c` | The hook (calls), the events and errors mirrors |
| `Kconfig` | `CONFIG_SYS_REMOTE_BOARDS` |
| `AGENTS.md` | Rules for changing this folder |

## Init

`sys_remote_init()`, a boot step after the session, the watches, the connectors and the events: sets sys_device's remote hook (`sys_device_set_remote`), which also tells it when a remote device is made or removed (to make and end the mirrors of its board).

## A call

The hook gets the device's record (`board`, `remote_id`, `mode`, `max_age_ms`) and the device command `req` sys_device made of the call (`[0x05][dir][remote_id u16][op u16][args]`: what the app sends a device), with no lock held but the caller's own:

| Call | `async` | `blocking` (a wired neighbour only, else `ERR_DEV_REMOTE_NOT_WIRED`) |
|---|---|---|
| write | `sys_session_call`: returns once the session took it; its answer comes on the RX task: ERROR is reported as the device's (`ERR_DEV_ID` over `ERR_REMOTE` over that board's chain), every OK answer then `sys_watch_stale(board, req, 2)` | `sys_session_call_wait`; then `sys_watch_stale(board, req, 2)` |
| read | `sys_watch_read(board, req, max_age_ms, false, …)` | `sys_watch_read(board, req, max_age_ms, true, …)` |

- `ERR_SESSION_BUSY` goes back as it is: "later", which the VM's device blocks take as such.
- `ERR_SESSION_NO_ROUTE` or `_PEER_DOWN`: its board is gone, `ERR_DEV_REMOTE_DOWN{device, board}`. No state is kept for it: the next call tries again, and works once that board is reached again.
- The args of a call are at most `SYS_DEVICE_REMOTE_ARGS_MAX` (64) bytes.

## Events and errors

For every board that has a remote device here (made with the first one, ended with the last), sys_remote makes two mirrors (SYS_DATA_CONNECTOR.MD "Mirrors") of that board's connectors, on connectors of this board (ids from `0xE0`, two per board), and is their listener:

- **`EVENTS_ALL`**: an event whose source is a remote device's `remote_id` is published here (`sys_event_publish_deferred`) with that device's local id as its source (`sys_device_find_remote`), its `t_ms` as this board's clock sets it, and `SYS_EVENT_LOCAL` (it stays here: a board that mirrors this one asks the device's own board); the others are dropped. So ON_EVENT and this board's listeners see it as a local device's.
- **`ERRORS`**: a record of a command that names a remote device's `remote_id` (`[0x05][dir][remote_id]…`) is reported here as that device's (`ERR_DEV_ID` over `ERR_REMOTE` over that board's chain: `err_record_parse`), so its policy answers it; a record of a command of this board (its `from`) is skipped, its answer brought it, and so is any other. The report runs on the connector task.

With no slot left (`CONFIG_SYS_REMOTE_BOARDS`) the board's devices work and their events and errors are not mirrored; that is reported as an error about the board (`ERR_CONN_FULL`, `err_report_board`), as is a mirror's lost frames (`ERR_CONN_MIRROR_LOST`): runit's policy per board answers them (RUNIT.MD "Error policy wiring").

## Needs elsewhere

- **sys_device**: the remote record, the hook, `sys_device_find_remote`, the remote devices made and removed (SYS_DEVICE.MD "Remote devices").
- **sys_session**: calls, the wait, `sys_session_wired`, `ERR_SESSION_BUSY` (SYS_SESSION.MD).
- **sys_watch**: `sys_watch_read`, `sys_watch_stale` (SYS_WATCH.MD "Cached reads").
- **sys_data_connector**: create, remove, mirror (SYS_DATA_CONNECTOR.MD "Mirrors").
- **sys_event**: `sys_event_publish_deferred`, `SYS_EVENT_LOCAL`.
- **errors**: `err_record_parse`.

## Open

- The policy per board has no wire command: the board config sets it in C.
- An async write's error comes back once from the device's next write, so a VM block sees it one pass later: now it is only reported.
- The VM's device blocks taking `ERR_SESSION_BUSY` as "later" and a failed CHANGE as not sent.
