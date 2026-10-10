# SYS_DATA_CONNECTOR — Transport-Agnostic Data Bus

IMPORTANT: When changing this component, update this document to match its API and behavior.

`sys_data_connector` connects logical data streams to transports. Everything above it (`sys_interface`, `vm_sub`, the error sink) names only a **connector ID** and never knows whether bytes travel over BLE, UART, Wi-Fi or MQTT. Everything transport-specific lives in a **provider** that the transport component implements, the same way a device adapter implements its contract.

```
 producers / consumers                connectors (logical)         providers (transports)
 ─────────────────────                ────────────────────         ──────────────────────
 error sink: text  ────────────────▶  0 logs       [0x03] ──TX──▶  BLE  (char 0xFFE3)
 error sink: packets ──────────────▶  1 errors     [0x04] ──TX──▶  BLE  (char 0xFFE3), UART
 vm_sub telemetry  ────────────────▶  2 telemetry  [0x02] ──TX──▶  BLE  (char 0xFFE1), UART
 sys_interface responses ─send_to──▶  3 interface  [0x05] ──TX──▶  BLE  (char 0xFFE1), UART (origin only)
 sys_interface RX task ◀──receive───  3 interface         ◀──RX──  BLE  (char 0xFFE2), UART
                                      4+ user connectors (class 0x06 packets)
```

UART is the console UART ("#R:<hex>" lines next to the text log, [[SYS_UART.MD]]), bound when `CONFIG_RUNIT_UART_CONSOLE_LINK=y`.

`[0xNN]` is the connector's **stream byte**, prepended to every outbound frame so a client can tell streams apart when several share one transport endpoint.

## Files

- `include/sys_data_connector.h`: public API, provider operations table, origin type.
- `sys_data_connector.c`: provider registry, connector registry, bindings, send / receive.
- `errors/sys_error_data_connector.h`: owners (`0xAD00`–`0xAD0B`) and tags (aggregated by `sys_errors`).
- `Kconfig`: registry sizes and `CONFIG_SYS_DATA_CONNECTOR_FRAME_MAX`.

### IDs and the app

Connector and provider IDs are configured in `components/utils/Kconfig` ("Data Connector Registry": `CONFIG_SYS_DATA_CONN_ID_*`, `CONFIG_SYS_DATA_PROVIDER_ID_*`). Code never uses those options directly; it uses two published enums whose values come from them:

| Enum | Where | Members |
|---|---|---|
| `sys_data_connector_id_e` | `include/sys_data_connector.h` | `SYS_DATA_CONNECTOR_LOGS / _ERRORS / _TELEMETRY / _INTERFACE`, and `_APP_BASE` (first ID for user connectors) |
| `runit_data_provider_e` | `components/runit/runit_board_defs.h` (the board decides which transports exist) | `RUNIT_DATA_PROVIDER_BLE`, `RUNIT_DATA_PROVIDER_UART` |

Both are `//#ref-enum`, so they are in `data-structures/enums.json`, and the class `0x06` packet fields reference them (`@enum-ref`) in `settings.generated.json`. The enum generator resolves `= CONFIG_*` member values from `sdkconfig`, so the app always gets the IDs the firmware was built with. After changing an ID in Kconfig, regenerate the enums and settings JSON.

## Concepts

### Connector
A registry slot (`0..CONFIG_SYS_DATA_CONNECTOR_MAX-1`) with a name, a stream byte, an optional frame cap (`max_frame`, including the stream byte; 0 = `CONFIG_SYS_DATA_CONNECTOR_FRAME_MAX`) and up to `CONFIG_SYS_DATA_CONNECTOR_PROVIDERS_MAX` TX and RX **bindings**. A binding is `(provider ID, endpoint)`; one provider appears at most once per direction.

- **TX is fan-out:** `sys_data_connector_send()` goes to every TX binding. `sys_data_connector_send_to(origin)` goes only to the TX binding of the origin's provider, addressed to the origin's peer.
- **RX is fan-in:** `sys_data_connector_receive()` pops one frame from the RX bindings in round-robin order (a busy provider can't starve the others) and reports its origin.

### Provider
A transport registered once at boot with `sys_data_connector_register_provider(id, &ops)`. Providers are never removed. `sys_data_connector_provider_t`:

| Op | Required | Contract |
|---|---|---|
| `send(endpoint, peer, frame, len)` | for TX | Queue one frame (stream byte included) to `peer` or to `SYS_DATA_CONNECTOR_PEER_ALL`. Return errors; don't drop them. |
| `dequeue(endpoint, buf, max, &len, &peer)` | for RX | Non-blocking. Return **one whole frame** and the peer it came from; `len = 0` when empty. A frame longer than `max` is dropped and reported, never cut short. |
| `max_frame(endpoint, peer)` | no | The largest frame the link carries **right now** (BLE: negotiated MTU − 3). NULL = `CONFIG_SYS_DATA_CONNECTOR_FRAME_MAX`. |
| `bind_rx(endpoint, conn_id)` / `unbind_rx` | no | Start / stop calling `sys_data_connector_notify_rx(conn_id)` whenever a frame is queued. |

**Framing belongs to the provider.** A message transport (BLE: one write = one frame) needs none. A byte-stream transport (UART, TCP) de-frames its bytes (length prefix, COBS, …) inside the provider and queues whole frames, so nothing above it ever sees partial frames.

**Endpoint** is the provider's own address for a binding: a plain `uint32_t` value from the board wiring or a settings packet, never a pointer. BLE: a characteristic UUID. UART: always 0. A binding isn't checked against the provider: a bad endpoint is accepted by `bind_tx` and fails only on send. An MQTT provider would map it to a topic slot in its own table.

**Peer** tells apart several clients on one endpoint. BLE has one connection and always reports 0; a TCP provider would report its session, so `send_to` answers the right client.

### Origin
`sys_data_connector_origin_t {provider_id, peer}` is filled by `receive()` for every frame. `sys_interface` keeps it for the frame being decoded and answers with `send_to()`, so clients on different transports (or different sessions) never see each other's responses.

### Frame limit
Buffers are static and sized to the compile-time cap `CONFIG_SYS_DATA_CONNECTOR_FRAME_MAX` (default 512): connector send, the `sys_interface` RX frame, `sys_actions` record / replay frames. The **runtime** limit is smaller when a provider reports one:

- `sys_data_connector_max_payload(id)` = the smallest limit over the connector's TX bindings and its own `max_frame`, minus the stream byte. Producers ask it per frame, not once (it follows the BLE MTU exchange): the error sink sizes error packets and cuts text lines to it, `vm_sub` sizes telemetry frames to it.
- A send longer than a binding's limit isn't sent there: `ERR_DATA_CONNECTOR_FRAME_TOO_LONG`. Nothing on the path truncates a frame.

### System connectors
`sys_data_connector_init()` creates logs, errors, telemetry and interface with `system = true`. Nothing may:

- remove or reconfigure (re-`create`) them: `ERR_DATA_CONNECTOR_PROTECTED`;
- unbind their last TX or last RX binding: output would go nowhere (responses silently lost), or the command channel the fix has to arrive on would be cut;
- suspend one that has RX bindings (the interface), for the same reason.

Moving a system connector to another transport is allowed: bind the new provider first, then unbind the old one. Suspending a TX-only system connector (logs, errors, telemetry) is allowed on purpose: it frees a slow link when a lot is going on.

## Public API

All functions take a connector ID and are safe from any task (not from an ISR). The registry is guarded by a mutex; providers are called on copies taken under it, except `bind_rx` / `unbind_rx` (lock order: connector → provider).

| Function | Notes |
|---|---|
| `sys_data_connector_register_provider(id, ops)` | Boot only. `ERR_INVALID_VAL_UI32` for ID 0 or a taken ID, `ERR_BASE_NO_MEM` when `CONFIG_SYS_DATA_PROVIDER_MAX` are registered. |
| `sys_data_connector_init()` | Creates the wake semaphores (static) and the system connectors. |
| `sys_data_connector_create(cfg)` | Create, or reconfigure a non-system connector (bindings are kept). |
| `sys_data_connector_exists(id)` | |
| `sys_data_connector_remove(id)` | Detaches every RX provider. |
| `sys_data_connector_bind_tx / unbind_tx(id, provider, [endpoint])` | Binding again re-addresses. `ERR_DATA_CONNECTOR_NO_PROVIDER` for an unknown provider or one without `send`. |
| `sys_data_connector_bind_rx / unbind_rx(id, provider, [endpoint])` | Re-addressing attaches the new endpoint before detaching the old one. |
| `sys_data_connector_suspend / resume(id)` | Suspended: send drops, receive returns nothing (frames wait in the providers). Resume wakes the consumer. |
| `sys_data_connector_max_payload(id)` | See Frame limit. 0 for an unknown connector. |
| `sys_data_connector_send(id, data, len)` | Every binding is tried; the first error is returned. Suspended / unbound: dropped, NULL. |
| `sys_data_connector_send_to(id, origin, data, len)` | NULL without sending when the origin's provider has no TX binding here. |
| `sys_data_connector_receive(id, buf, max, &len, &origin)` | Non-blocking, round-robin. |
| `sys_data_connector_wait_rx(id, timeout_ms)` | Blocks on the slot's wake semaphore. The semaphore is static and survives remove / create, so a blocked consumer never holds a dead handle. |
| `sys_data_connector_notify_rx(id)` | Called by providers (not from an ISR). |

## Settings packets (class `0x06`)

`codecs/decoders/dec_settings_data_connector.h`, class `CONFIG_RX_PACKET_CLASS_SETTINGS_DATA_CONNECTOR`:

| Packet | Payload | Call |
|---|---|---|
| `0x01` create | id, header (stream byte), max_packet_len (frame cap incl. stream byte, 0 = default), name (NUL-terminated) | `create` (`system = false`) |
| `0x02` remove | id | `remove` |
| `0x03` / `0x04` RX add / remove | connector id, provider id, [endpoint u32] | `bind_rx` / `unbind_rx` |
| `0x05` / `0x06` TX add / remove | connector id, provider id, [endpoint u32] | `bind_tx` / `unbind_tx` |
| `0x07` / `0x08` suspend / resume | id | `suspend` / `resume` |

`provider_param` on the wire is the endpoint (BLE: characteristic UUID).

## Board wiring (runit)

`runit_board_connector_bindings_init()` registers the BLE provider (`sys_ble_provider_register(RUNIT_DATA_PROVIDER_BLE)`, [[SYS_BLE.MD]]) and binds the four system connectors to the runIT characteristics, as in the diagram above. With `CONFIG_RUNIT_UART_CONSOLE_LINK` it also registers the console UART provider ([[SYS_UART.MD]]) and binds errors, telemetry and interface (TX + RX) to it. Adding a transport means: implement `sys_data_connector_provider_t` in that transport's component, give it a `CONFIG_SYS_DATA_PROVIDER_ID_*` and a member of `runit_data_provider_e`, register and bind it in runit, and regenerate the JSON. Nothing in this component or above changes.

## Errors

| Tag | Level | Payload | Raised when |
|---|---|---|---|
| `ERR_DATA_CONNECTOR_FRAME_TOO_LONG` (`0xAD01`) | MEDIUM | `uint8_t id; uint8_t provider_id; uint32_t len; uint32_t max;` | A frame is longer than the connector cap (`provider_id` 0) or a binding's current limit. |
| `ERR_DATA_CONNECTOR_PROTECTED` (`0xAD02`) | LOW | `uint8_t id;` | Removing or reconfiguring a system connector, unbinding its last TX or RX binding, or suspending one that receives. |
| `ERR_DATA_CONNECTOR_NO_PROVIDER` (`0xAD03`) | LOW | `uint8_t provider_id;` | Binding an unregistered provider, or one without the op that direction needs. |

Base tags: `ERR_BASE_NOT_FOUND` (unknown connector), `ERR_BASE_NO_MEM` (binding / provider slots full), `ERR_INVALID_VAL_UI32` (bad ID or `max_frame`).
