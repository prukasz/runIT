# sys_remote — devices of other boards (draft)

Component `components/sys/sys_remote/` (draft in `_wire_draft/`, not built). It carries the remote devices of this board (SYS_DEVICE_REMOTE.MD) to their boards and back: sys_device knows only where a device is and hands its calls to the hook this component sets; the session, the watches and the mirrors do the carrying. Nothing depends on it, so it may use them all: a board without other boards leaves it out of its boot, and its remote devices then answer `ERR_DEV_REMOTE_DOWN`.

```
 sys_device_call (a remote device) ── hook ──► sys_remote ─┬─ write: sys_session_call / _call_wait, then sys_watch_stale
                                                           └─ read:  sys_watch_read (a copy, a watch, or a request now)
 that board's EVENTS_ALL ─► mirror ─► sys_remote: source → the local id ─► sys_event_publish ─► ON_EVENT, listeners
 that board's ERRORS     ─► mirror ─► sys_remote: request → the local id ─► err_report as the device's ─► its policy
```

## Files

| File | Role |
|---|---|
| `sys_remote.h` | Use: init |

## Init

`sys_remote_init()`, a boot step after the session, the watches, the connectors and the events: sets sys_device's remote hook (`sys_device_set_remote`) and listens for the remote devices made and removed (to make and end the mirrors of their boards).

## A call

The hook gets the device's record (`board`, `remote_id`, `mode`, `max_age_ms`) and the device command `req` sys_device made of the call (`[0x05][dir][remote_id][op][args]`: what the app sends a device), with no lock held but the caller's own:

| Call | `async` | `blocking` (a wired neighbour only, else `ERR_DEV_REMOTE_NOT_WIRED`) |
|---|---|---|
| write | `sys_session_call`: returns once the session took it; its answer comes on the RX task: ERROR reported as the device's (`ERR_DEV_ID` over `ERR_REMOTE` over that board's chain), every answer then `sys_watch_stale(board, req, 2)` | `sys_session_call_wait`; then `sys_watch_stale(board, req, 2)` |
| read | `sys_watch_read(board, req, max_age_ms, false, …)` | `sys_watch_read(board, req, max_age_ms, true, …)` |

- `ERR_SESSION_BUSY` goes back as it is: "later", which the VM's device blocks take as such.
- `ERR_SESSION_NO_ROUTE` or `_PEER_DOWN`: its board is gone, `ERR_DEV_REMOTE_DOWN{device, board}`. No state is kept for it: the next call tries again, and works once that board is reached again.
- An `async` write's error also comes back once from the device's next write, so a VM block sees it one pass later.

## Events and errors

For every board that has a remote device here (made with the first one, ended with the last), sys_remote makes two mirrors (SYS_DATA_CONNECTOR.MD "Mirrors") of that board's connectors, and is their listener:

- **`EVENTS_ALL`**: an event whose source is a remote device's `remote_id` is published here (`sys_event_publish`) with that device's local id as its source (`sys_device_find_remote`), its `t_ms` as it came; the others are dropped. So ON_EVENT and this board's listeners see it as a local device's.
- **`ERRORS`**: a record whose request names a remote device's `remote_id` is reported here as that device's (`ERR_DEV_ID` over `ERR_REMOTE` over that board's chain: `err_record_parse`), so its policy answers it; a record of a command of this board (its `from`) is skipped, its answer brought it.
- A gap in a mirror's `seq` is reported by the connector, as an error of that board (`ERR_CONN_MIRROR_LOST`), which runit's policy for that board answers.

## Needs elsewhere

- **sys_device**: the remote record, the hook, `sys_device_find_remote`, the remote devices made and removed (SYS_DEVICE_REMOTE.MD).
- **sys_session**: calls, the wait, `sys_session_wired`, `ERR_SESSION_BUSY` (SYS_SESSION.MD).
- **sys_watch**: `sys_watch_read`, `sys_watch_stale` (SYS_WATCH.MD "Cached reads").
- **sys_data_connector**: mirrors (SYS_DATA_CONNECTOR.MD "Mirrors").
- **sys_event**: `sys_event_publish` with another source.
- **errors**: `err_record_parse` (ERRORS.MD "Error record").
- **runit**: `sys_remote_init` in the boot after the modules above, when the board config has other boards.

## Open

- The `.c` file.
