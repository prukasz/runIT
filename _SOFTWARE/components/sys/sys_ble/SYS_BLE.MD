# sys_ble — BLE peripheral, runtime GATT table, router link (for agents)

Component `components/sys/sys_ble/`. NimBLE peripheral with a GATT table of 16-bit services and characteristics that can change at runtime. The runIT service carries the router link; user services / characteristics (e.g. one a VM program streams temperature over) are created at runtime through the `ble` router.

## Files

| File | Role |
|---|---|
| `sys_ble.h` | Public API, runIT UUIDs, fragment header, error tags |
| `sys_ble_priv.h` | Registry nodes, context, internal API between the two core files |
| `sys_ble.c` | Registry (create / remove / lock), sync to NimBLE, TX queue + BLE task step, RX delivery, connection state |
| `sys_ble_stack.c` | NimBLE init, advertising, GAP events, GATT access callbacks, table compile, notify |
| `sys_ble_link.c/.h` | runIT service + `TRANSPORT_BLE` (fragment reassembly → `router_submit`, answers on TX) |
| `sys_ble_router.c/.h` | `ble` router (`ROUTER_KEY_BLE` = 0x03): runtime GATT changes, status |
| `Kconfig` | Name, appearance, task prio/stack, TX item max, apply delay, listeners |

## GATT table (same as legacy)

| UUID | Name | Props | Buffer | Framed | Use |
|---|---|---|---|---|---|
| `0xFFE0` | runIT service | primary, **locked** | | | |
| `0xFFE1` | runit TX | notify | TX 4 frames | yes | router answers (later telemetry) |
| `0xFFE2` | runit RX | write | RX handler | yes | router requests |
| `0xFFE3` | runit LOGS | notify | TX 4 frames | yes | logs / errors stream (not ported yet) |
| `0xFFE4` | runit Status | notify | TX 512 B | no | kept from legacy, unused |

The UUID defines keep their `//@STATIC_SERVICE` / `//@STATIC_CHARACTERISTIC` annotations (moved from legacy `runit_board_defs.h`). Device name `CONFIG_SYS_BLE_DEVICE_NAME` ("runit").

## Framing (new)

A **framed** characteristic carries frames longer than one ATT packet. Every packet starts with one header byte:

```
bit 7 LAST · bit 6 FIRST · bits 0-5 packet counter (mod 64, 0 at FIRST)
single-packet frame: C0 <frame>
3-packet frame:      40 <…>  01 <…>  82 <…>
```

- **TX**: an item (whole frame, up to `CONFIG_SYS_BLE_TX_ITEM_MAX` = 512) is split by the BLE task into packets of MTU − 4 bytes; the task sends one item's packets back to back, so frames from different tasks never interleave.
- **RX** (`sys_ble_link.c`): FIRST resets the reassembly; a wrong counter or a packet outside a frame → `ERR_BLE_FRAG` (logged) and the frame is dropped up to the next FIRST; overflow → `ERR_BLE_TOO_LONG`; LAST → `router_submit(TRANSPORT_BLE, 0, frame)`.
- So router frames up to 512 B work at any MTU (20-byte link before the MTU exchange: 19 B per packet). The app must add / strip the header on 0xFFE1 / 0xFFE2 / 0xFFE3.
- Unframed characteristics send one item per packet (item ≤ MTU − 3).

## Connector provider

`sys_ble_link_init` also registers `TRANSPORT_BLE` as a connector provider (SYS_CONNECTOR.MD): **endpoint = characteristic UUID**, `send` = `sys_ble_char_send(uuid, frame, 20 ms)`, `max_frame` = `sys_ble_char_max_payload(uuid)`. Board bindings (`runit_board_cfg.c`): logs + errors → LOGS 0xFFE3, telemetry → TX 0xFFE1. A user connector can be bound to any user characteristic with TX — that is how a VM program will stream (temp → 0xEE01).

## Data paths

- **TX** `sys_ble_char_send(uuid, data, len, wait_ms)`: not connected → dropped (NULL). Queued only when it fits `sys_ble_char_max_payload(uuid)` (framed: TX buffer / `TX_ITEM_MAX`; unframed: also MTU − 3) — `ERR_BLE_TOO_LONG`; buffer full past `wait_ms` → `ERR_BLE_TX_FULL`. Sent once the client subscribed (CCCD). Notifications or indications per `is_indicate`. NimBLE `BLE_HS_ENOMEM` is flow control: retried every 10 ms while the same link is up.
- **RX** buffer (`rx_buffer_size` > 0): writes queued, polled with `sys_ble_char_rx_dequeue` (empty → NULL, len 0). Full → `ERR_BLE_RX_FULL` (logged, counted in status).
- **RX handler** (`sys_ble_char_set_rx_handler`): each write passed to the handler on the host task, outside the registry lock, must not block. For characteristics without an RX buffer (the router link).
- `sys_ble_char_rx_inject`: feeds the RX path as if the peer wrote (tests).
- Buffer sizes: `SYS_BLE_BUFF_SIZE_FOR(item_max, count)` — a no-split ring buffer holds items of only about half its byte size.

## Runtime changes

Same model as legacy: changes are **staged** and applied by `sys_ble_apply()` (router `APPLY`).

- Adding / removing a characteristic of a live service marks it dirty; the sync deletes the service from NimBLE, frees removed characteristics and re-adds it (client subscriptions on it reset). Removing a live service deletes it at the next sync (removals first, so remove + create of one UUID in a batch works). Lookups skip a service being removed.
- **Locked** service (runIT): no characteristic changes, no removal → `ERR_BLE_SVC_LOCKED`. User characteristics go in user services.
- With a client connected the sync runs `CONFIG_SYS_BLE_APPLY_DELAY_MS` (200 ms) later on the BLE task, so the APPLY command is answered first. Windows drops every GATT object on the Service Changed indication; the client reconnects.
- UUIDs are unique across services and characteristics (`ERR_BLE_UUID_TAKEN`).
- NimBLE calls that lock the host run without the registry lock (GATT callbacks take it).

## Connection

- On connect: MTU = the link's current MTU (a client may finish the exchange before the connect event), then the device starts the exchange itself, offering NimBLE's `CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU` (527 = NimBLE max in `sdkconfig`; lower it to test fragmentation from a PC, which negotiates 527).
- On disconnect: TX buffers drained, subscriptions cleared, advertising restarted, listeners called.
- `sys_ble_add_conn_listener(fn)` (boot, up to `CONFIG_SYS_BLE_MAX_CONN_LISTENERS`): `fn(connected, reason)` on the host task. The link uses it to reset reassembly and call `router_link_lost()` (dead-man with `on_link_lost`).
- `sys_ble_get_status`: connected, MTU, RX overflow count.

## Remote (`sys_ble_router`, root key `ROUTER_KEY_BLE` = 0x03)

| Op | Request | Response |
|---|---|---|
| `0x01` SVC_CREATE | `u16 uuid, u8 is_primary` | – |
| `0x02` SVC_REMOVE | `u16 uuid` | – |
| `0x03` CHAR_CREATE | `u16 svc_uuid, u16 uuid, u8 write, u8 indicate, u8 notify, u8 framed, u32 tx_buffer, u32 rx_buffer`, name (UTF-8, NUL-terminated, nothing after the NUL (`cmd_name_arg`) → 0x2901 descriptor) | – |
| `0x04` CHAR_REMOVE | `u16 svc_uuid, u16 uuid` | – |
| `0x05` APPLY | – | – (applied ~200 ms later with a client) |
| `0x06` STATUS | – | `u8 connected, u16 mtu, u32 rx_overflow` |

Example — a user service with a temperature characteristic a VM program can stream on (notify, unframed, TX 96 B); bytes after `[seq][flags]`:
`03 01 00 EE 01` (SVC_CREATE 0xEE00 primary), `03 03 00 EE 01 EE 00 00 01 00 60 00 00 00 00 00 00 00 74 65 6D 70 00` (CHAR_CREATE 0xEE01 "temp", TX 96 B), `03 05` (APPLY).

## Errors (`ERR_MOD_SYS_BLE` = 4)

`ERR_BLE_STACK{rc}` HIGH, `_ADV{rc}` MEDIUM, `_GATT{uuid, rc}` MEDIUM, `_SVC_LOCKED{uuid}` LOW, `_UUID_TAKEN{uuid}` LOW, `_NOT_FOUND{uuid}` MEDIUM, `_NO_MEM{uuid, size}` HIGH, `_NO_TX{uuid}`, `_NO_RX{uuid}`, `_TX_FULL{uuid, len}`, `_RX_FULL{uuid, len}`, `_TOO_LONG{uuid, len, max}` MEDIUM, `_BAD_ARG` HIGH, `_FRAG{uuid, hdr, expected}` LOW, `_RX_HANDLER{uuid}` MEDIUM. Errors on the host / BLE task are logged (`sys_ble_report`).

## Changes from legacy (`legacy/components/system/ble`)

- Router link instead of a `sys_data_connector` provider; RX of the command link by handler (no RX buffer, no wake callbacks / dequeue task).
- **Framing** on the runIT TX / RX / LOGS characteristics (legacy refused frames above MTU − 3).
- `sys_buffers` dropped: ESP no-split ring buffers directly (`SYS_BLE_BUFF_SIZE_FOR`).
- No `sys_event` publish yet (not ported): connection listeners instead.
- `sys_ble_database_sync` / `_apply` → `sys_ble_start` / `sys_ble_apply`; settings class `0x02` → `ble` router; `char_create` gained `is_framed`.
- The board setup (`runit_board_ble_init`) moved into `sys_ble_link_init`.

## Open

- App: the fragment header and the link (connect, reconnect by itself) are in `app/src/ble/` (`RunitBleLink`, tests in `app/tests/ble/`); the new router frame format is not on the app side yet.
- Advertising carries the runIT service (0xFFE0) next to the name, so a browser can filter by it and `watchAdvertisements` tells it when a known board is back. Built, not run on the air. No bonding (`sm_bonding = 0`): the OS never reconnects by itself, the app does.
- LOGS (errors / logs stream) and telemetry on TX wait for the data connectors.
- VM streaming over a user characteristic: the VM side (write `sys_ble_char_send` on a block) is not ported.
- Not run on hardware yet.
