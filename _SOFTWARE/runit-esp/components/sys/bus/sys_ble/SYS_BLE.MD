# sys_ble — the board's BLE peripheral

Component `runit-esp/components/sys/bus/sys_ble/`. NimBLE as a peripheral (one client, no pairing) with a GATT table of 16-bit services and characteristics that changes at run time through its router. A characteristic is a way out (notify / indicate, with a TX buffer) and in (the client writes it); one hook per characteristic hears its writes and the link's state. The module knows no providers: `provider_ble` (PROVIDER_BLE.MD) makes one of a characteristic, or of a pair. The board is not a central.

| File | Role |
|---|---|
| `sys_ble.h` | `sys_ble_start`, the table's calls, `sys_ble_hook`, `sys_ble_send`; the service and characteristic flags and the fragment header bits |
| `internal/sys_ble_internal.h` | The nodes, the lock, what the sources share (the table's calls for the router) |
| `sys_ble.c` | The table (staged), the sync to NimBLE, the hooks, TX (the BLE task), RX and the framing, the connection |
| `sys_ble_stack.c` | NimBLE: init, advertising, GAP events, GATT access, the table's definitions, notify |
| `router/sys_ble_router.h` / `.c` | The remote interface (root key `MODULE_ID_BLE` 0x2E): the frames and flags, then `BLE_OPERATIONS`; registers itself |
| `router/contract_ble.json` | The router's descriptor |
| `sys_ble_id.h`, `errors/` | `MODULE_ID_DECLARE(MODULE_ID_BLE, 0x2E)` (`sys_ble_id.h`), `ERR_SYS_BLE_MAP`, `errors_sys_ble.json` |
| `Kconfig` | The name, the BLE task, the apply delay |

## Boot

`sys_ble_start(adv_service)` (once, in `app/runit`, after the board's first providers) adds the services staged so far to NimBLE, starts it, advertising of `adv_service` (a client filters by it) and the BLE task. The module has no service of its own: the characteristics are made by the providers (`provider_ble`: its CREATE makes its service and characteristics, PROVIDER_BLE.MD), and the board's own pair by `app/runit` (RUNIT.MD "The board's data"). Before the start they are only staged; after it, each CREATE or REMOVE of a provider is applied live.

## The table

`sys_ble_service_create / _remove / _lock`, `sys_ble_char_create / _remove` and `sys_ble_apply` (`sys_ble.h`) are the providers' calls, the only ones that change the table. A service **locked** with `sys_ble_service_lock` (the board's own, after its characteristics) takes no change and no removal, because a rebuild would drop the client's subscriptions under its session. A characteristic's TX buffer is `SYS_BLE_TX_BYTES(frames)` bytes. The UUIDs of the runIT service are `app/runit`'s (`runit_ids.h`). The fragment header bits are published to the app as the enum group `ble_frag` (markers in `sys_ble.h`). Logs and errors reach a client as its session's streams (DATA_MATRIX.MD, RUNIT.MD). The device name is `CONFIG_SYS_BLE_DEVICE_NAME` (default `runit`).

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

`sys_ble_hook(uuid, cb, ctx, &framed)` takes a characteristic's hook (one per characteristic, `ERR_BLE_HOOK` if another `ctx` holds it) and tells whether it is framed; `cb` NULL lets it go (a gone characteristic: NULL) and waits for the call of a hook in flight: once let go, a hook is never called again (never from a hook). On the host task (the state also on the task that changed it), outside the lock, one hook call at a time (`g_ble_calls`, taken before the lock; not the lock itself, since a hook may send), must not block:

- `cb(ctx, data, len)`: one write (a whole frame on a framed one). A write with no hook is refused (ATT "write not permitted").
- `cb(ctx, NULL, 1)`: a client is connected; `cb(ctx, NULL, 0)`: it left, or the characteristic is removed (CHAR_REMOVE, SVC_REMOVE: at once, not at APPLY), and then the hook is let go. A taken hook hears the state at once.

## Data paths

`sys_ble_send(uuid, data, flags, wait_ms)`: no client → dropped (NULL). Else the item is queued on the characteristic's TX buffer (a no-split ring buffer), at most its largest item and `CMD_FRAME_MAX` (`ERR_BLE_TOO_LONG`), none `ERR_BLE_NO_TX`. A full buffer is polled for `wait_ms`, then `ERR_BLE_TX_FULL`. `CMD_SEND_SPARE` is `ERR_BLE_TX_FULL` once the frames waiting (counted as frames of `CMD_FRAME_MAX`, not as free bytes: a no-split ring's free bytes are only its longest run) are half the buffer: the rest is kept for the answers. Items keep their order: `CMD_SEND_URGENT` goes in turn. The BLE task sends items once the client has subscribed (CCCD); NimBLE's `BLE_HS_ENOMEM` is retried every 10 ms while the same link is up. On disconnect the TX buffers are emptied.

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
- Advertising carries the service `sys_ble_start` was given (`app/runit`'s runIT service) next to the name, so a client can filter by it. No pairing: `CONFIG_BT_NIMBLE_SECURITY_ENABLE` is off.

## Remote (`sys_ble_router`, root key `MODULE_ID_BLE` 0x2E)

Reads only. The table is changed by the providers (PROVIDER_BLE.MD): one way to make a service or a characteristic. What the module itself will be set with (a password, pairing) goes through this router.

| Op | Side | Args | Answer |
|---|---|---|---|
| `0x01` STATUS | R | – | `sys_ble_status_t` `{u8 connected, u16 mtu}` |
| `0x02` LIST | R | – | `u8 count`, per service `{u16 uuid, u8 flags (bit 0 primary, bit 1 locked), u8 n}`, then n × `{u16 uuid, u8 flags, u16 tx, name NUL-ended}`: the table as the next `sys_ble_apply` leaves it |

A characteristic's flags: bit 0 WRITE, 1 INDICATE, 2 NOTIFY, 3 FRAMED (`SYS_BLE_CHR_*`). A TX buffer needs NOTIFY or INDICATE (`ERR_CMD_BAD_ARG`); it holds about half its bytes as one item.

Example: a user characteristic a stream sends on (notify, unframed): PROVIDER CREATE type BLE, cfg `{svc 0xEE00, tx 0xEE01, rx 0, flags 0, name "temp"}` (PROVIDER_BLE.MD), then the data matrix `CONNECT` of the stream's port to that provider's port (DATA_MATRIX.MD).

## Errors (`MODULE_ID_BLE` 0x2E)

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

## Tests

On a board, from the PC's Python with bleak (`pip install bleak`): `python runit-esp/tools/ble_client.py` opens a session as client 0xF1 on the runIT service and runs PING and PEERS; `--smoke` adds GPIO 4's MODE and LEVEL and a PERIODIC watch whose entries it reads from the raw telemetry characteristic `0xFFEA` (a BLE session does not carry the watches); `--hold N` keeps a watch N seconds and counts the entries by owner. With `session_client.py` on the console at the same time, PEERS shows both sessions open and each client sees the other's writes and watch entries. Run by hand on a board: the matrix's LIST over BLE, `DISCONNECT` / `CONNECT` of `WATCH` to the telemetry provider (the entries stop and come back on `0xFFEA`), refused links and commands (the ERROR answers, the records as PUSH frames of `ERRORS` on the session), the log lines on `0xFFE9` (each a `[hdr PUSH]` packet: the 5 header bytes, then the text), the events on the session after STOP and SAFE_LEAVE.

## Open

- The app's settings tab makes the user characteristics as BLE providers (`PROVIDER CREATE` of type BLE) and reads this table with LIST; not run on a board yet.
