# BLE Manager (sys_ble)

IMPORTANT: If you modify the source files in this component (sys_ble.h or sys_ble.c), you must update this documentation to reflect the changes.

## Overview
The `sys_ble` component implements a robust, asynchronous wrapper around the NimBLE host stack for Bluetooth Low Energy (BLE) operations. It abstracts GATT database compilation, connection lifecycle tracking, and memory management. It features non-blocking, priority-based transmission slots using `sys_tx_buff_t` ring buffers and thread-safe reception buffers (RX ring buffers) to decouple application logic from BLE stack execution.

### Directory Map
```
components/system/ble/
├── CMakeLists.txt
├── SYS_BLE.MD             # This documentation
├── sys_ble.c              # Core NimBLE host integrations and thread workers
└── include/
    ├── sys_ble.h          # GATT configuration, TX/RX slot definitions, and API declarations
    └── sys_ble_status_codes.h # BLE system owner and status codes definitions
```

---

## Architecture & Concept

### Asynchronous TX Slots (sys_tx_buff_t)
To prevent blocking the calling thread during BLE packet transmission, `sys_ble` implements priority-capable transmission slots:
* Up to 3 custom TX buffers (`MAX_TX_BUFFERS = 3`) can be assigned per characteristic.
* Transmit buffers use `sys_tx_buff_t` structures from `sys_buffers`, supporting both split-free ring buffers (`RINGBUF_TYPE_NOSPLIT_BUF`) and byte ring buffers (`RINGBUF_TYPE_BYTE_BUF`).
* Transmissions support automatic packet header prepending (`tx_buff.header`) and can be sent as Notifications or Indications.

### Dynamic Callback Event Routing
BLE events (connect, disconnect, stack failure) trigger callback notifications via `SYS_BLE_CB`:
```c
SYS_BLE_CB(event_id, event_value, mask);
```
Submodules subscribe to events using `sys_ble_add_callback(...)`.

### Thread-Safe RX Queuing
When data is written by a peer to a characteristic:
1. The NimBLE callback writes the payload directly into the characteristic's dedicated RX ring buffer.
2. The manager signals the registered FreeRTOS binary semaphore (`rx_sem`).
3. The client application wakes up, reads the data using `sys_ble_char_rx_dequeue(...)`, and processes it.

---

## Core Data Structures & Enums

### Service Configuration (sys_ble_svc_cfg_t)
```c
typedef struct {
  uint16_t uuid;      // 16-bit Service UUID
  bool is_primary;    // Primary service flag
} sys_ble_svc_cfg_t;
```

### Characteristic Creation (sys_ble_char_create_t)
```c
typedef struct {
  sys_ble_char_cfg_t info; // Properties (UUID, read/write/notify/indicate flags, descriptor name)
  size_t rx_buffer_size;   // Dedicated RX ring buffer allocation size
} sys_ble_char_create_t;
```

### TX Buffer Configuration (sys_ble_tx_buf_cfg_t)
```c
typedef struct {
  uint16_t buffer_id;      // Unique buffer slot identifier
  size_t size;             // Ring buffer capacity (bytes)
  bool is_indication;      // Notify (false) or Indicate (true) transfer mode
  sys_tx_buff_t tx_buff;   // Embedded system buffer descriptor (ringbuf handle, type, header, item size)
} sys_ble_tx_buf_cfg_t;
```

### BLE Status (sys_ble_status_t)
```c
typedef struct {
  bool is_connected;
  uint16_t mtu_size;
  uint32_t rx_overflow_count;
} sys_ble_status_t;
```

### BLE Events (sys_ble_events_e)
```c
typedef enum sys_ble_events_e {
  SYS_BLE_EVENT_CONNECT = 0,
  SYS_BLE_EVENT_DISCONNECT,
  SYS_BLE_EVENT_FAILURE,
  SYS_BLE_EVENT_MAX
} sys_ble_events_e;
```

---

## Utility Macros

* **`SYS_BLE_CB(event_id, event_value, mask)`**: Construct a `cb_event_t` with `CALLBACK_BLE` type, attach route mask, and post event to `sys_callback_trigger`.

---

## API Functions

### Initialization & Lifecycle APIs
* **`sys_ble_init()`**:
  Initializes controller and NimBLE host stack, initializes RX/TX mutexes, and spawns background BLE worker tasks.
* **`sys_ble_add_callback(on_event, route_mask)`**:
  Registers an event route mask for BLE stack events (`SYS_BLE_EVENT_CONNECT`, `SYS_BLE_EVENT_DISCONNECT`, etc.).
* **`sys_ble_database_sync()`**:
  Compiles temporary GATT services and characteristics database definitions and registers them live to the active NimBLE stack. Must be called after changing GATT layouts.
* **`sys_ble_get_status(out_status)`**:
  Fetches MTU size, active connection state, and RX overflow count telemetry.

### GATT Service APIs
* **`sys_ble_service_create(cfg)`**:
  Declares a new GATT service definition.
* **`sys_ble_service_remove(svc_uuid)`**:
  Removes a service definition (and its child characteristics) from the registry.

### GATT Characteristic APIs
* **`sys_ble_char_create(svc_uuid, cfg)`**:
  Declares a new characteristic under the specified service UUID.
* **`sys_ble_char_remove(svc_uuid, char_uuid)`**:
  Removes a characteristic definition from its parent service.

### Telemetry & Transfer APIs
* **`sys_ble_char_assign_tx_buffer(char_uuid, buf_cfg)`**:
  Allocates a priority-based TX ring buffer slot for non-blocking packet sends.
* **`sys_ble_char_get_rx_semaphore(char_uuid, out_sem)`**:
  Retrieves the FreeRTOS binary semaphore handle triggered by peer write events.
* **`sys_ble_char_rx_dequeue(char_uuid, buffer, max_len, out_len)`**:
  Safely pulls incoming peer write data from the characteristic's RX ring buffer.
* **`sys_ble_char_send(char_uuid, buffer_id, data, len, return_when_full)`**:
  Queues data for transmission into the specified TX ring buffer slot.
