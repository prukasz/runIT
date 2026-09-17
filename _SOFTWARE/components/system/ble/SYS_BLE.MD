# BLE Manager (sys_ble)

IMPORTANT: When changing this component, update this document to match its API and behavior.

## Files

- `include/sys_ble.h`: public service/characteristic configuration and transport API.
- `sys_ble_priv.h`: registry nodes, context, and helpers shared between source files.
- `sys_ble.c`: registry management, buffer access, connector linking, and database synchronization.
- `sys_ble_stack.c`: NimBLE initialization, advertising, GAP/GATT callbacks, GATT compilation, and TX worker.
- `include/sys_error_ble.h`: BLE error and owner definitions.

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

BLE transports bytes; connector framing belongs to `sys_data_connector`.
`sys_ble_char_link_connector()` attaches a connector to an RX-enabled
characteristic. Successful peer writes and `sys_ble_char_rx_inject()` share
the same enqueue helper, overflow counter, and connector semaphore signaling.
Consumers drain whole items through `sys_ble_char_rx_dequeue()`; an empty
buffer returns success with zero length. Unlinking removes the wake target.

`sys_ble_char_send()` queues an already-framed item and wakes the TX worker.
It supports immediate return on a full buffer or a wait of up to 100 ms.
The worker scans registered services directly, skipping pending additions,
pending removals, and characteristics without TX buffers or assigned handles.
There is no cached TX array or fixed TX-slot limit. Packet bytes and transport
parameters are copied under the registry mutex before transmission; node
pointers are not retained by the sender during retries. Sync wakes the worker
to resume data queued before a characteristic became live.

`cfg.is_indicate` selects indications; otherwise TX uses notifications.
Buffer items are not fragmented: dequeue truncates items exceeding its
destination capacity (TX uses the negotiated MTU payload capacity).
Use appropriately sized frames.

## Events and status

`SYS_BLE_CB(event, value)` automatically queues connect, disconnect, and failure
events to `SYS_CB_ROUTE_BLE` and `SYS_CB_ROUTE_VM`. Action IDs are zero;
there is no per-event callback/action configuration in the BLE context.
The BLE route currently logs events; the VM route stores them for VM consumers.

`sys_ble_get_status()` reports connection state, MTU, and RX overflow count.
