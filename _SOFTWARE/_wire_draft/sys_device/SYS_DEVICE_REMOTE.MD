# sys_device — remote devices (draft)

What `components/sys/sys_device/` gains (draft in `_wire_draft/`, not built): a **remote device** is a device of this board of any type (a servo, an expander, a motor) that is a device of another board. The registry knows only where it is and hands its calls to a hook; sys_remote (SYS_REMOTE.MD) sets the hook and carries them, with the session, the watches and the mirrors. sys_device depends on none of them.

```
 VM / wrapper / driver           sys_device (this board)                         sys_remote
 ─────────────────────           ───────────────────────                         ──────────
 sys_device_call(id, …)  ──►  local: the driver's op, as always
                              remote: checks the op, req = [0x05][dir][remote_id][op][args] ── hook ──► that board
```

## Files

| File | Role |
|---|---|
| `sys_device_remote.h` | The remote record (`sys_device_remote_t`), its modes, the hook (`sys_device_remote_ops_t`), set, find |

## A remote device

CREATE_REMOTE, an op of the DEVICE contract (number: the next free `0x00xx`): `[type u16][sys_device_remote_t]`. `sys_device_remote_t` (6 B): `board`, `remote_id`, `mode`, `max_age_ms`. Its own config is the other board's (that device was created there); create asks that board nothing and returns at once. The registry keeps its type, so its ops are checked here from that type's own table (sizes, sides), and a VM block, a wrapper, a driver or the app sees a device of that type. `type` must be one this firmware knows (both boards run firmware of one `descriptors_hash`, which their hellos carry).

That board knows nothing of it: the device there is its own device, which its own program, the app and any number of other boards use as well. Only a frame's `src` tells who sent a call, as for any command.

## Modes

| Mode | A write | A read | Way |
|---|---|---|---|
| `async` | returns once the session took it; its answer comes later | a copy at most `max_age_ms` old, else `ERR_WATCH_NO_VALUE` / `_STALE`; never waits | any |
| `blocking` | waits for the answer, at most the wired timeout | a copy at most `max_age_ms` old, else asks now and waits (`max_age_ms` 0: always asks) | a `WIRED` neighbour only, else `ERR_DEV_REMOTE_NOT_WIRED` |

The app sets the mode and `max_age_ms` per device. A device another device references (its `pin_ref`, a device it requires) must be `blocking`: CREATE of the referencing one refuses an `async` one (`ERR_DEV_BAD_PIN`, `ERR_DEV_BAD_ARG`). Its driver then calls it as a device of this board, which answers a round trip later, about a millisecond on a cable, or at once from a copy.

## A call

`sys_device_call` on a remote device checks the op and its args against the type's table under the registry lock, copies the record, lets the lock go, makes the call into the **device command** for that board, `req` = `[0x05][dir][remote_id][op][args]` (the same command the app sends a device), and calls the hook's `call` **without the registry lock**, as a guard is called (SYS_DEVICE.MD "Guards"): so a blocking call holds its caller only. Called from inside another device's op (a `pin_ref`), the registry lock that op holds stays held for the round trip: the cost of `blocking`, a millisecond on a cable. A failure is wrapped in `ERR_DEV_ID` as any device's. No hook set (a board without sys_remote): `ERR_DEV_REMOTE_DOWN`.

What the hook does (a write as a call, a read as a cached read, `ERR_SESSION_BUSY` as "later", a gone board as `ERR_DEV_REMOTE_DOWN`) is sys_remote's (SYS_REMOTE.MD "A call"). The registry keeps no state of the other board: a call to a gone board fails, and the next one works once it is reached again.

## The hook

`sys_device_set_remote(const sys_device_remote_ops_t* ops)`, boot, one: `call(record, req, out, cap, &out_len)`, and `added(board)` / `removed(board)` when a remote device of that board is made or removed (sys_remote makes and ends that board's mirrors with them). `sys_device_find_remote(board, remote_id)`: the local id of a remote device, `SYS_DEVICE_ID_NONE` when there is none; sys_remote turns that board's events and errors into this device's with it.

## Order across boards

A write to a device of this board is done in the pass that makes it; an `async` write to a remote one when its answer comes, a round trip later. Where a device must not act before a device of another board did (B after A), A is `blocking`, a wired neighbour: its write returns when it ran, so B, later in the same pass, comes after it. Over a radio nothing waits: a step that must follow a device of another board waits in the program for that device's state (a value it reads back), or runs on that board.

When a board is gone, it does nothing for a gone user of its device: the device keeps its last state, as when a program of that board stops writing to it. What stops it is that board's own safety: its dead-man (its parent's session), the root's STOP to every board when the app is gone, and its own program.

## Errors (`ERR_MOD_SYS_DEVICE`)

| Error | Severity | Fields | When |
|---|---|---|---|
| `ERR_DEV_REMOTE_DOWN` | HIGH | `u16 id, u8 board` | its board is gone, or no sys_remote on this board |
| `ERR_DEV_REMOTE_NOT_WIRED` | MEDIUM | `u16 id, u8 board` | a `blocking` device whose board is no wired neighbour |

## Needs elsewhere

- **sys_remote**: the hook (SYS_REMOTE.MD).
- **vm_blocks**: `ERR_SESSION_BUSY` as "later", a failed CHANGE sent again (`_wire_draft/payloads.h`).
- **The app**: creates a remote device where this board's program or devices use a device of another board, with its mode and `max_age_ms`; a device another one references only `blocking`, and no cycle of blocking devices between boards.

## Open

- The code in sys_device: the record, CREATE_REMOTE, the call without the registry lock, the hook, `sys_device_find_remote`.
