# sys_remote — devices of other boards

Component `components/sys/sys_remote/`. It carries the remote devices of this board (SYS_DEVICE.MD "Remote devices") to their boards and back: sys_device knows only where a device is and hands its calls to the hook this component sets; the session, the watches and the mirrors do the carrying. Nothing depends on it, so it may use them all: a board without other boards leaves it out of its boot (`runit` starts it when the board has a link, `RUNIT_LINK_ROOT` / `RUNIT_LINK_CHILD`), and its remote devices then answer `ERR_DEV_REMOTE_DOWN`.

```
 sys_device_call (a remote device) ── hook ──► sys_remote ─┬─ write: sys_session_call / _call_wait, then sys_watch_stale
                                                           └─ read:  sys_watch_read (a copy, a watch, or a request now)
 that board's EVENTS ─► mirror ─► sys_remote: source → the local id ─► sys_event_publish_deferred ─► ON_EVENT, listeners
```

## Files

| File | Role |
|---|---|
| `sys_remote.h` | Use: init |
| `sys_remote.c` | The hook (calls), the events mirrors |
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

## Events

For every board that has a remote device here (made with the first one, ended with the last), sys_remote makes a mirror (SYS_DATA_CONNECTOR.MD "Mirrors") of that board's `EVENTS` connector, on a connector of this board (ids from `0xE0`, one per board), and listens to it. A NOTIFY event whose source is a remote device's `remote_id` is published here (`sys_event_publish_deferred`) with that device's local id as its source (`sys_device_find_remote`); the others are dropped. So ON_EVENT and this board's listeners see it as a local device's. With no slot left (`CONFIG_SYS_REMOTE_BOARDS`) the board's devices work and their events are not mirrored.

## Needs elsewhere

- **sys_device**: the remote record, the hook, `sys_device_find_remote`, the remote devices made and removed (SYS_DEVICE.MD "Remote devices").
- **sys_session**: calls, the wait, `sys_session_wired`, `ERR_SESSION_BUSY` (SYS_SESSION.MD).
- **sys_watch**: `sys_watch_read`, `sys_watch_stale` (SYS_WATCH.MD "Cached reads").
- **sys_data_connector**: create, remove, mirror (SYS_DATA_CONNECTOR.MD "Mirrors").
- **sys_event**: `sys_event_publish_deferred`.

## Open

- The errors of a remote device's board as that device's own (a mirror of the `ERRORS` connector), the board as an error source (`ERR_CONN_MIRROR_LOST`), and the events of every kind (`EVENTS_ALL`, `SYS_EVENT_LOCAL`): step 5 of the wire plan.
- An async write's error comes back once from the device's next write, so a VM block sees it one pass later: now it is only reported.
- The VM's device blocks taking `ERR_SESSION_BUSY` as "later" and a failed CHANGE as not sent.
