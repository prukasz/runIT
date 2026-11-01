# sys_ble — BLE peripheral, runtime GATT table, the runIT providers

Component `components/sys/sys_ble/`. NimBLE peripheral with a GATT table of 16-bit services and characteristics that can change at runtime. The runIT service carries two providers (SYS_DATA_PROVIDER.MD): the runIT pair, a client's session, and the LOGS characteristic; user services and characteristics are created at runtime through the `ble` router, and an unframed one that writes with an RX handler or has a TX buffer is a provider too (below).

## Files

| File | Contents |
|---|---|
| `core/sys_ble.h` | Public API, runIT UUIDs, fragment header, buffer size macro |
| `errors/sys_ble_errors.h` | `ERR_SYS_BLE_MAP`, the module's error tags (included by `sys_ble.h`) |
| `errors/errors_sys_ble.json` | The error messages |
| `core/sys_ble_priv.h` | Registry nodes, context, internal API between the core files |
| `core/sys_ble.c` | Registry (create / remove / lock), sync to NimBLE, TX queue and BLE task step, RX delivery, connection state |
| `core/sys_ble_stack.c` | NimBLE init, advertising, GAP events, GATT access callbacks, table compile, notify |
| `link/sys_ble_link.c/.h` | The runIT service and its two providers (`SYS_BLE_PROVIDER_RUNIT`, `SYS_BLE_PROVIDER_LOGS`), and the providers of run-time characteristics: their driver |
| `router/sys_ble_router.c/.h` | `ble` router (`ROUTER_KEY_BLE` = 0x03): runtime GATT changes, status |
| `contract_ble.json` | The router's descriptor (`templates/`) |
| `Kconfig` | Name, appearance, BLE task, apply delay, connection listeners, the providers' send wait |
| `AGENTS.md` | Rules for changing this folder |

## Boot

`ble link` (`sys_ble_link_init`) builds the runIT service and adds its providers. It runs after `router` and before `stream bindings` and `sessions`. `ble` (`sys_ble_start`) builds NimBLE and starts advertising. It runs only with `CONFIG_RUNIT_BLE_START` (default on), after `system devices`. Boot steps: `components/app/runit/runit.c`.

## The GATT table

| UUID | Name | Props | Buffer | Framed | Use |
|---|---|---|---|---|---|
| `0xFFE0` | runIT service | primary, **locked** | | | |
| `0xFFE1` | runit TX | notify | TX 4 frames | yes | the session's frames to the client: answers, stream frames (errors, telemetry, events), control |
| `0xFFE2` | runit RX | write | RX handler | yes | the session's frames from the client: requests, control |
| `0xFFE3` | runit LOGS | notify | TX 4 frames | yes | the logs connector's lines as they are (no header) |
| `0xFFE4` | runit Status | notify | TX 512 B | no | created; nothing sends on it yet |

"4 frames" is `SYS_BLE_BUFF_SIZE_FOR(CMD_FRAME_MAX, 4)`. The UUIDs and the fragment header bits are published to the app: the groups `ble_runit_uuid` and `ble_frag` in `runit-app/generated/common/enums.json` (markers in `sys_ble.h`). The device name is `CONFIG_SYS_BLE_DEVICE_NAME` (default `runit`).

## Framing

A **framed** characteristic carries frames longer than one ATT packet. Every packet starts with one header byte:

```
bit 7 LAST · bit 6 FIRST · bits 0-5 packet counter (mod 64, 0 at FIRST)
single-packet frame: C0 <frame>
3-packet frame:      40 <…>  01 <…>  82 <…>
```

- **TX**: an item (a whole frame, up to `CMD_FRAME_MAX`, 512) is split by the BLE task into packets of MTU − 4 bytes. The task sends one item's packets back to back, so frames from different tasks never interleave.
- **RX** (`sys_ble_link.c`): FIRST resets the reassembly. A wrong counter or a packet outside a frame reports `ERR_BLE_FRAG`, and the rest of the frame is dropped up to the next FIRST. Overflow reports `ERR_BLE_TOO_LONG`. LAST hands the frame to the runIT provider's user (`sys_data_provider_deliver`: the session).
- A frame of up to `CMD_FRAME_MAX` bytes works at any MTU: 19 bytes per packet on the 20-byte link before the MTU exchange. The app adds and strips the header on 0xFFE1, 0xFFE2 and 0xFFE3.
- An unframed characteristic sends one item per packet (item ≤ MTU − 3).

## Providers

`sys_ble_link_init` adds two framed providers (SYS_DATA_PROVIDER.MD), each named by its characteristic: `SYS_BLE_PROVIDER_RUNIT` = `{BLE, 0xFFE1}` (the pair: frames in on RX 0xFFE2, out on TX 0xFFE1) and `SYS_BLE_PROVIDER_LOGS` = `{BLE, 0xFFE3}` (out only). Both are up (`max_frame` = `CMD_FRAME_MAX`) while a client is connected, down (0) otherwise: the connection listener sets it. `send` = `sys_ble_char_send(uuid, frame, wait)`: with `NOWAIT` the wait is 0, else `CONFIG_SYS_BLE_ANSWER_WAIT_MS` (100 ms); a full buffer is `ERR_PROV_BUSY`. The characteristics' buffers keep their order, so `URGENT` goes in turn. The board config attaches a client session to the pair and binds the logs connector to LOGS (`components/app/runit/`). No VM block writes to a characteristic yet.

**Run-time characteristics as providers.** CHAR_CREATE (and `sys_ble_link_char_add`) makes a provider `{BLE, uuid}` of a characteristic that is **unframed** and either writes with no RX buffer (an rx provider: each write is one message to the provider's user, on the host task, at most MTU − 3 bytes) or has a TX buffer (a tx provider: `send` is one notification, `sys_ble_char_send`). A framed one, or one with neither, gets none. These providers are unframed (`FRAMED` clear), up while a client is connected, with `max_frame` the characteristic's largest item now (the TX buffer's, the MTU less the ATT header; a write-only one the MTU's): the MTU exchange tells the connection listeners again, so it follows the MTU. At most `CONFIG_SYS_BLE_USER_PROVIDERS` (a static table; they take slots of `CONFIG_SYS_DATA_PROVIDER_MAX` too): past that, or an id taken, CHAR_CREATE is refused and the characteristic is not kept. CHAR_REMOVE and SVC_REMOVE remove the providers first: one that a connector's binding or a session has open is `ERR_PROV_IN_USE` and nothing is removed. A connector binds one with BIND (SYS_DATA_CONNECTOR.MD): `BIND_RX` takes what the peer writes, `BIND_TX` notifies it.

## Data paths

- **TX** `sys_ble_char_send(uuid, data, len, wait_ms)`: not connected → dropped (NULL). Queued only when it fits `sys_ble_char_max_payload(uuid)`: the TX buffer limit for a framed characteristic; for an unframed one also MTU − 3 (`ERR_BLE_TOO_LONG` otherwise). A full buffer is polled for `wait_ms`, then `ERR_BLE_TX_FULL`. Items are sent once the client has subscribed (CCCD), as notifications or indications per the characteristic. NimBLE `BLE_HS_ENOMEM` is flow control: retried every 10 ms while the same link is up.
- **RX** buffer (`rx_buffer_size` > 0): writes are queued and polled with `sys_ble_char_rx_dequeue` (empty → NULL, length 0). A full buffer drops the write: `ERR_BLE_RX_FULL` (reported, counted in the status).
- **RX handler** (`sys_ble_char_set_rx_handler`): each write goes to the handler on the host task, outside the registry lock. The handler must not block. This is the path of a characteristic without an RX buffer (the runIT RX).
- `sys_ble_char_rx_inject`: feeds the RX path as if the peer had written the bytes (tests).
- A buffer is a no-split ring buffer, which holds an item of about half its byte size. `SYS_BLE_BUFF_SIZE_FOR(item_max, count)` sizes it for `count` items.

## Runtime changes

Changes are **staged** and applied by `sys_ble_apply()` (router `APPLY`).

- Adding or removing a characteristic of a live service marks the service dirty. The next sync deletes the service from NimBLE, frees removed characteristics and adds the service again. Client subscriptions on it reset.
- Removing a live service deletes it at the next sync. Removals run first, so a remove and a create of one UUID in one batch works. Lookups skip a service being removed.
- **Locked** service (runIT): no characteristic changes, no removal → `ERR_BLE_SVC_LOCKED`. User characteristics go in user services.
- With a client connected, the sync runs `CONFIG_SYS_BLE_APPLY_DELAY_MS` (default 200 ms) later on the BLE task, so the command that asked for it is answered first. The client may drop all GATT objects on the Service Changed indication (Windows does) and reconnects.
- Characteristic UUIDs are unique across the whole table (`ERR_BLE_UUID_TAKEN`). Service UUIDs are unique among services. A service and a characteristic may share a UUID.
- NimBLE calls that lock the host run without the registry lock, because the GATT callbacks take it.

## Connection

- On connect, the MTU is the link's current MTU (a client may finish the exchange before the connect event). The device then starts the exchange itself, offering `CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU` (527 in `sdkconfig`, the NimBLE maximum). A PC negotiates 527.
- Connection parameters: interval 7.5 ms, latency 0, supervision timeout 2 s. The central may refuse them or round them up.
- On disconnect: TX buffers are drained, subscriptions are cleared, advertising restarts and the listeners are called.
- Advertising carries the runIT service (0xFFE0) next to the name, so a client can filter by it. No bonding (`sm_bonding = 0`): the app reconnects by itself.
- `sys_ble_add_conn_listener(fn)` (boot, up to `CONFIG_SYS_BLE_MAX_CONN_LISTENERS`): `fn(connected, reason)` on the host task. The runIT providers use it to reset reassembly and go up or down; a client's session ends when its provider goes down (SYS_SESSION.MD), which is the dead-man's lost link.
- The module publishes no `sys_event` events. Connection changes reach the listeners only.
- `sys_ble_get_status`: connected, MTU, RX overflow count.

## Remote (`sys_ble_router`, root key `ROUTER_KEY_BLE` = 0x03)

Requests are `[0x03][dir][op][args]` (`dir` 0 read, 1 write; ROUTER.MD). STATUS and LIST are reads, the other ops writes.

| Op | Request | Response |
|---|---|---|
| `0x01` SVC_CREATE | `u16 uuid, u8 is_primary` | – |
| `0x02` SVC_REMOVE | `u16 uuid` | – |
| `0x03` CHAR_CREATE | `u16 svc_uuid, u16 uuid, u8 write, u8 indicate, u8 notify, u8 framed, u32 tx_buffer, u32 rx_buffer`, then the name (UTF-8, NUL-terminated, the last field; a non-empty name becomes the descriptor 0x2901) | – |
| `0x04` CHAR_REMOVE | `u16 svc_uuid, u16 uuid` | – |
| `0x05` APPLY | – | – (applied `CONFIG_SYS_BLE_APPLY_DELAY_MS` later with a client) |
| `0x06` STATUS | – | `u8 connected, u16 mtu, u32 rx_overflow` |
| `0x07` LIST | – | the table as it is after the next apply: `u8 count`, then per service `u16 uuid, u8 flags (bit0 primary, bit1 locked), u8 n`, then n × `u16 uuid, u8 flags (bit0 write, bit1 indicate, bit2 notify, bit3 framed), u32 tx, u32 rx, name (NUL-terminated)` |

The app reads LIST to see what a save must change, so it removes only what is not wanted.

Example: a user service with a temperature characteristic that a VM program can stream on (notify, unframed, TX 96 B). The commands (key, dir, op, args): `03 01 01 00 EE 01` (SVC_CREATE 0xEE00, primary), then `03 01 03 00 EE 01 EE 00 00 01 00 60 00 00 00 00 00 00 00 74 65 6D 70 00` (CHAR_CREATE 0xEE01 "temp", TX 96 B), then `03 01 05` (APPLY). The session adds its own frame header (SYS_SESSION.MD "Frames").

## Errors (`ERR_MOD_SYS_BLE` = 4)

| Error | Severity | Fields |
|---|---|---|
| `ERR_BLE_STACK` | HIGH | `rc` |
| `ERR_BLE_ADV` | MEDIUM | `rc` |
| `ERR_BLE_GATT` | MEDIUM | `uuid, rc` |
| `ERR_BLE_SVC_LOCKED` | LOW | `uuid` |
| `ERR_BLE_UUID_TAKEN` | LOW | `uuid` |
| `ERR_BLE_NOT_FOUND` | MEDIUM | `uuid` |
| `ERR_BLE_NO_MEM` | HIGH | `uuid, size` |
| `ERR_BLE_NO_TX` | MEDIUM | `uuid`: no TX buffer, or being removed |
| `ERR_BLE_NO_RX` | MEDIUM | `uuid`: not writable, or no RX buffer |
| `ERR_BLE_TX_FULL` | MEDIUM | `uuid, len`: item dropped |
| `ERR_BLE_RX_FULL` | MEDIUM | `uuid, len`: peer write dropped |
| `ERR_BLE_TOO_LONG` | MEDIUM | `uuid, len, max` |
| `ERR_BLE_BAD_ARG` | HIGH | – |
| `ERR_BLE_FRAG` | LOW | `uuid, hdr, expected`: packet out of order, frame dropped |
| `ERR_BLE_RX_HANDLER` | MEDIUM | `uuid`: handler already set, or the characteristic has an RX buffer |

Errors raised on the host or BLE task have no caller to return to. They go through `sys_ble_report`: logged, and sent on the errors stream.

## Limits

- No bonding: the link reconnects by itself, with no stored keys.
- The runIT service is locked: its TX, RX, LOGS and Status characteristics cannot be changed at runtime.
- Verified on hardware from Windows (the app's backend through `bleak`, `runit-app/tests/hardware`): the MTU of 527, the framing at the full MTU (requests in packets of 20 to 512 B, long answers), advertising and reconnecting, a board restart, and a runtime service added with `APPLY` while a client was connected (the client's OS reports no disconnect and invalidates its handles: the app reconnects). Unverified: a central that negotiates a smaller MTU, Android, and the removal of a characteristic of a live service.
