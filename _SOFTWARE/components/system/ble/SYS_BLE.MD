# BLE Manager (sys_ble)

IMPORTANT: When changing this component, update this document to match its API and behavior.

## Files

- `include/sys_ble.h`: public service/characteristic configuration and transport API.
- `sys_ble_priv.h`: registry nodes, context, and helpers shared between source files.
- `sys_ble.c`: registry management, buffer access, connector linking, and database synchronization.
- `sys_ble_stack.c`: NimBLE initialization, advertising, GAP/GATT callbacks, GATT compilation, and TX worker.
- `sys_ble_provider.c` / `include/sys_ble_provider.h`: BLE as a `sys_data_connector` transport provider.
- `errors/sys_error_ble.h`: BLE error and owner definitions (aggregated by `sys_errors`).

## Configuration and startup

Create services with `sys_ble_service_create()`, then create characteristics with
`sys_ble_char_create(svc_uuid, cfg)`. Passing `SYS_BLE_SVC_DEFAULT_AUTO` creates
the default primary service lazily.

`sys_ble_char_cfg_t` contains the UUID, write/notify/indicate properties, optional
description, and TX/RX buffer sizes. Each nonzero size allocates one
`sys_buff_t`; zero disables that buffer. Allocation failure cleans up the new
characteristic before it is inserted into the registry.

Call `sys_ble_database_sync()` after configuring the registry. Its first call
compiles the database and starts NimBLE, advertising, and the TX task.
There is no separate `sys_ble_init()`: the mutex and wake semaphore are
constructed at load time. Subsequent sync calls apply runtime changes.

## Runtime changes

Services and characteristics can be created and removed after startup.
Characteristic UUIDs are unique across the registry.

- Adding a characteristic to a registered service marks that service dirty.
- Removing a characteristic from a registered service marks it pending removal.
  Its buffers and node remain alive until the service is deleted from NimBLE.
- Sync skips unchanged services. A dirty service is deleted, its pending
  removals are freed, and its remaining characteristics are compiled and added.
  New services use the same compile/add path.
- If compilation or re-add fails after deletion, the service remains
  unregistered so the next sync can retry.
- Removing a service deletes it immediately from NimBLE before freeing its nodes.
- NimBLE's dynamic add/delete APIs generate Service Changed notifications.

The initial compiled database is retained in the context because NimBLE
references its definitions. Later compiled definitions are owned per service.

## Data connector integration

BLE transports bytes; the stream byte belongs to `sys_data_connector`. BLE is a
transport **provider** of the connector bus, like a device adapter implements
its contract: `sys_ble_provider.c` (`include/sys_ble_provider.h`) implements
`sys_data_connector_provider_t`, and runit registers it with
`sys_ble_provider_register(RUNIT_DATA_PROVIDER_BLE)`. The dependency
runs one way, `ble` → `sys_data_connector`; the connector knows no transport.

| Provider op | BLE |
|---|---|
| endpoint | 16-bit characteristic UUID (a larger value is `ERR_INVALID_VAL_UI32`) |
| framing | none needed: one peer write = one frame, one notification = one frame |
| peer | always 0 (one connection) |
| `send` | `sys_ble_char_send(uuid, …, return_when_full = true)`; errors are returned |
| `dequeue` | `sys_ble_char_rx_dequeue()` |
| `max_frame` | `sys_ble_char_max_payload(uuid)` |
| `bind_rx` / `unbind_rx` | `sys_ble_char_link_rx_wake()` / `unlink`, with a callback that calls `sys_data_connector_notify_rx(conn_id)` |

The BLE core itself stays consumer-agnostic: `sys_ble_char_link_rx_wake(uuid,
wake, ctx)` registers an opaque wake callback on an RX-enabled characteristic.
Successful peer writes and `sys_ble_char_rx_inject()` share the same enqueue
helper, overflow counter, and wake callbacks.
Consumers drain whole items through `sys_ble_char_rx_dequeue()`; an empty
buffer returns success with zero length. Unlinking removes the wake target.

### MTU

`g_ble_ctx.mtu_size` is the real ATT MTU: on connect, the link's current MTU
(`ble_att_mtu()`, 23 before any exchange), then the result of the MTU exchange
(`BLE_GAP_EVENT_MTU`). A client that starts the exchange itself (Windows) can
finish it before the connect event is delivered, so the MTU event may come
first; the connect handler must not reset it to 23 (fixed 2026-09-24: every
frame was capped at 20 B on Windows). The device doesn't
wait for the client: on connect it requests the preferred MTU itself
(`ble_gattc_exchange_mtu`, preferred = `BLE_ATT_MTU_MAX`; `BLE_HS_EALREADY`
when the client already started it is fine). **runIT needs the larger MTU.** Frames
aren't split, so on a client that stays at 23 bytes, anything longer than 20
bytes is refused (`ERR_DATA_CONNECTOR_FRAME_TOO_LONG`); that is accepted
rather than fragmenting.

`sys_ble_char_max_payload(uuid)` returns the most one item on that
characteristic carries now: the smaller of MTU − 3 and the largest item its TX
buffer accepts (a no-split ringbuffer takes items of only half its size,
[[SYS_BUFFERS.MD]]). While disconnected only the buffer limits it. The
connector uses it as the provider's frame limit, so producers size frames to
the link and the buffer instead of having them cut.

### Buffer sizes

Size characteristic buffers in frames, not bytes:
`SYS_BUFF_SIZE_FOR(CONFIG_SYS_DATA_CONNECTOR_FRAME_MAX, n)`. The runIT
characteristics (runit_board_cfg.c) use 2 frames for RX and 4 for TX and logs.
A buffer created from a settings packet with a byte size takes items of about
half that size; the provider reports that as its frame limit.

`sys_ble_char_send()` queues an already-framed item and wakes the TX worker.
It supports immediate return on a full buffer or a wait of up to 100 ms.
The worker scans registered services directly, skipping pending additions,
pending removals, and characteristics without TX buffers or assigned handles.
There is no cached TX array or fixed TX-slot limit. Packet bytes and transport
parameters are copied under the registry mutex before transmission; node
pointers are not retained by the sender during retries. Sync wakes the worker
to resume data queued before a characteristic became live.

`cfg.is_indicate` selects indications; otherwise TX uses notifications.
Buffer items are not fragmented. An item longer than the current MTU payload is
dropped by `sys_buff_pop()` (`ERR_BUFFERS_ITEM_TOO_LONG`), never sent cut short;
through the connector this can't happen, because the provider's frame limit is
checked before the item is queued. A peer write longer than a characteristic's
RX buffer is refused the same way on dequeue.

## Events and status

`sys_ble_publish(event, value)` publishes connect, disconnect and failure events
(`SYS_EVENT_DOMAIN_BLE`, [[SYS_EVENT.MD]]). Nothing is routed by default: the
program or an action gets them through a subscription.

`sys_ble_get_status()` reports connection state, MTU, and RX overflow count.
`sys_ble_char_max_payload(uuid)` reports a characteristic's current item limit (see MTU).
