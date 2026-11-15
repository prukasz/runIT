# sys_ble — the board's BLE peripheral

Component `runit-esp-rework/components/sys/bus/sys_ble/`. NimBLE as a peripheral (one client, no pairing) with a GATT table of 16-bit services and characteristics that changes at run time through its router. A characteristic is a way out (notify / indicate, with a TX buffer) and in (the client writes it); one hook per characteristic hears its writes and the link's state. The module knows no providers: `provider_ble` (PROVIDER_BLE.MD) makes one of a characteristic, or of a pair. The board is not a central.

| File | Role |
|---|---|
| `sys_ble.h` | `sys_ble_init`, `sys_ble_hook`, `sys_ble_send`; the runIT UUIDs and the fragment header bits |
| `internal/sys_ble_internal.h` | The nodes, the lock, what the sources share (the table's calls for the router) |
| `sys_ble.c` | The table (staged), the sync to NimBLE, the hooks, TX (the BLE task), RX and the framing, the connection; `MODULE_ID_DEFINE(BLE, 0x2E)` |
| `sys_ble_stack.c` | NimBLE: init, advertising, GAP events, GATT access, the table's definitions, notify |
| `router/sys_ble_router.h` / `.c` | The remote interface (root key `MODULE_ID_BLE` 0x2E): the frames and flags, then `BLE_OPERATIONS`; registers itself |
| `router/contract_ble.json` | The router's descriptor |
| `errors/` | `MODULE_ID_DECLARE(BLE, 0x2E)` (`ERR_MOD_SYS_BLE`), `ERR_SYS_BLE_MAP`, `errors_sys_ble.json` |
| `Kconfig` | The name, the BLE task, the apply delay |

## Boot

`sys_ble_init` (once, in `app/runit`) creates the runIT service, locks it and starts NimBLE, advertising and the BLE task. `app/runit` then creates the provider of the runIT pair (`provider_ble`, `{tx 0xFFE1, rx 0xFFE2}`, system) and attaches a session to it, not told (`SYS_SESSION_UNKNOWN`: the hello says a client).

## The runIT service

| UUID | Name | Props | TX | Framed | Use |
|---|---|---|---|---|---|
| `0xFFE0` | runIT service | primary, **locked** | | | |
| `0xFFE1` | runit TX | notify | 4 frames | yes | the session's frames to the client: answers, PUSH frames (streams, watches), control |
| `0xFFE2` | runit RX | write | | yes | the session's frames from the client |

The UUIDs and the fragment header bits are published to the app as the enum groups `ble_runit_uuid` and `ble_frag` (markers in `sys_ble.h`). Logs and errors reach a client as its session's streams (SYS_STREAM.MD). The device name is `CONFIG_SYS_BLE_DEVICE_NAME` (default `runit`).

## Framing

A **framed** characteristic carries frames longer than one ATT packet. Every packet starts with one header byte:

```
bit 7 LAST · bit 6 FIRST · bits 0-5 packet counter (mod 64, 0 at FIRST)
single-packet frame: C0 <frame>
3-packet frame:      40 <…>  01 <…>  82 <…>
```

- **TX**: an item (up to `CMD_FRAME_MAX`) goes as packets of MTU − 3 bytes, the header byte included. The BLE task sends one item's packets back to back, so items never interleave.
- **RX**: the host task joins the packets into one frame per framed characteristic the client writes. FIRST starts a frame; a wrong counter or a packet outside a frame is `ERR_BLE_FRAG` and the frame is dropped up to the next FIRST; more than `CMD_FRAME_MAX` is `ERR_BLE_TOO_LONG`. LAST hands the whole frame to the hook.
- It works at any MTU: 19 bytes per packet on the 23-byte link before the exchange.
- An unframed characteristic sends an item as notifications of MTU − 3 bytes each, in order, and hands each write to the hook as it is.

## Hooks

`sys_ble_hook(uuid, cb, ctx, &framed)` takes a characteristic's hook (one per characteristic, `ERR_BLE_HOOK` if another `ctx` holds it) and tells whether it is framed; `cb` NULL lets it go (a gone characteristic: NULL). On the host task, outside the lock, must not block:

- `cb(ctx, data, len)`: one write (a whole frame on a framed one). A write with no hook is refused (ATT "write not permitted").
- `cb(ctx, NULL, 1)`: a client is connected; `cb(ctx, NULL, 0)`: it left, or the characteristic is removed (CHAR_REMOVE, SVC_REMOVE: at once, not at APPLY), and then the hook is let go. A taken hook hears the state at once.

## Data paths

`sys_ble_send(uuid, data, flags, wait_ms)`: no client → dropped (NULL). Else the item is queued on the characteristic's TX buffer (a no-split ring buffer), at most its largest item and `CMD_FRAME_MAX` (`ERR_BLE_TOO_LONG`), none `ERR_BLE_NO_TX`. A full buffer is polled for `wait_ms`, then `ERR_BLE_TX_FULL`. `CMD_SEND_SPARE` is `ERR_BLE_TX_FULL` while less than half the buffer is free: the rest is kept for the answers. Items keep their order: `CMD_SEND_URGENT` goes in turn. The BLE task sends items once the client has subscribed (CCCD); NimBLE's `BLE_HS_ENOMEM` is retried every 10 ms while the same link is up. On disconnect the TX buffers are emptied.

## Run-time changes

Changes are **staged** and applied by APPLY on the BLE task: at once with no client, `CONFIG_SYS_BLE_APPLY_DELAY_MS` (200 ms) later with one, so the command that asked for it is answered first.

- Adding or removing a characteristic of a live service marks it dirty: the next sync deletes it from NimBLE, frees the removed characteristics and adds it again. The client's subscriptions on it reset; the client may drop all its GATT objects on the Service Changed indication (Windows does) and reconnects.
- A removed service is deleted at the next sync. Removals run first, so a remove and a create of one UUID in one batch work. Lookups skip what is being removed.
- The runIT service is **locked**: no characteristic changes, no removal (`ERR_BLE_SVC_LOCKED`).
- Characteristic UUIDs are unique across the table (`ERR_BLE_UUID_TAKEN`); service UUIDs among services.
- NimBLE calls that wait on the host run without the registry lock, because the GATT callbacks take it.

## Connection

- The board asks for the connection parameters only (interval 7.5 ms, latency 0, supervision timeout 2 s; the central may refuse or round them up). The MTU exchange, the PHY and the packet length are the central's: a request from the board collides with the central's and can leave a link-layer procedure unanswered, and the link drops exactly 40 s after connecting (reason `0x222`). The board offers `CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU` (527) when the client asks; a PC negotiates 527.
- On connect the MTU is the link's current one (a client may finish the exchange before the connect event).
- On disconnect: TX buffers emptied, subscriptions and half-joined frames dropped, advertising restarts, the hooks hear down.
- Advertising carries the runIT service (0xFFE0) next to the name, so a client can filter by it. No pairing: `CONFIG_BT_NIMBLE_SECURITY_ENABLE` is off.

## Remote (`sys_ble_router`, root key `MODULE_ID_BLE` 0x2E)

| Op | Side | Args | Answer |
|---|---|---|---|
| `0x01` SVC_CREATE | W | `sys_ble_svc_args_t` `{u16 uuid, u8 flags}` (bit 0 primary) | – |
| `0x02` SVC_REMOVE | W | `sys_ble_uuid_args_t` `{u16 uuid}` | – |
| `0x03` CHAR_CREATE | W | `sys_ble_char_args_t` `{u16 svc, u16 uuid, u8 flags, u16 tx}`, then the name (NUL-ended; none: nothing), the descriptor 0x2901 | – |
| `0x04` CHAR_REMOVE | W | `sys_ble_uuid_args_t` `{u16 uuid}` | – |
| `0x05` APPLY | W | – | – (applied on the BLE task) |
| `0x06` STATUS | R | – | `sys_ble_status_t` `{u8 connected, u16 mtu}` |
| `0x07` LIST | R | – | `u8 count`, per service `{u16 uuid, u8 flags (bit 0 primary, bit 1 locked), u8 n}`, then n × `{u16 uuid, u8 flags, u16 tx, name NUL-ended}`: the table as the next APPLY leaves it |

A characteristic's flags: bit 0 WRITE, 1 INDICATE, 2 NOTIFY, 3 FRAMED (`SYS_BLE_CHR_*`). A TX buffer needs NOTIFY or INDICATE (`ERR_CMD_BAD_ARG`); it holds about half its bytes as one item. The app reads LIST to see what a save must change.

Example: a user service with a characteristic a stream sends on (notify, unframed, TX 96 B): `2E 01 01 00 EE 01` (SVC_CREATE 0xEE00, primary), `2E 01 03 00 EE 01 EE 04 60 00 74 65 6D 70 00` (CHAR_CREATE 0xEE01 "temp"), `2E 01 05` (APPLY), then a provider of it: PROVIDER CREATE type BLE, cfg `{tx 0xEE01, rx 0}` (PROVIDER_BLE.MD). The session adds its own frame header.

## Errors (`ERR_MOD_SYS_BLE` = `MODULE_ID_BLE` 0x2E)

| Error | Severity | Fields |
|---|---|---|
| `ERR_BLE_STACK` | HIGH | `rc`: NimBLE's init, a host call, a notify |
| `ERR_BLE_ADV` | MEDIUM | `rc` |
| `ERR_BLE_GATT` | MEDIUM | `uuid, rc`: adding or deleting a service |
| `ERR_BLE_SVC_LOCKED` | LOW | `uuid` |
| `ERR_BLE_UUID_TAKEN` | LOW | `uuid` |
| `ERR_BLE_NOT_FOUND` | MEDIUM | `uuid` |
| `ERR_BLE_NO_MEM` | HIGH | `uuid, size` |
| `ERR_BLE_NO_TX` | MEDIUM | `uuid` |
| `ERR_BLE_TX_FULL` | MEDIUM | `uuid, len` |
| `ERR_BLE_TOO_LONG` | MEDIUM | `uuid, len, max` |
| `ERR_BLE_FRAG` | LOW | `uuid, hdr, want`: a packet out of order, the frame dropped |
| `ERR_BLE_HOOK` | MEDIUM | `uuid`: another hook holds it |

Errors on the host or BLE task (a sync, a frame dropped, advertising) have no caller: they are reported (`err_report`).

## Open

- Not run on a board yet. The old firmware's version was verified from Windows (MTU 527, the framing at the full MTU, reconnects, a service added with a client connected); this one drops the board's own MTU request and pairing, and moves the joining of frames and the split of unframed items into this module.
- The app still talks to the old firmware's BLE (router key 0x03, the old CHAR_CREATE and LIST, the LOGS characteristic 0xFFE3): it follows when it moves to the rework.
