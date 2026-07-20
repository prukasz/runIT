# BLE Manager (sys_ble)

IMPORTANT: If you modify the source files in this component (sys_ble.h or sys_ble.c), you must update this documentation to reflect the changes.

## Overview
The `sys_ble` component implements a robust, asynchronous wrapper around the NimBLE host stack for Bluetooth Low Energy (BLE) operations. It abstracts GATT database compilation, connection lifecycle tracking, and memory management. It features non-blocking, priority-based transmission queues (TX ring buffers) and thread-safe reception buffers (RX ring buffers) to decouple application logic from BLE stack execution.

### Directory Map
```
components/system/ble/
├── CMakeLists.txt
├── SYS_BLE.MD             # This documentation
├── sys_ble.c              # Core NimBLE host integrations and thread workers
└── include/
    └── sys_ble.h          # GATT configuration, queue, and API definitions
```

---

## Architecture & Concept

### Asynchronous Priority TX Slots
To prevent blocking the calling thread during BLE packet transmission, `sys_ble` implements a priority queue slots system:
* Up to 4 custom TX buffers (`MAX_TX_BUFFERS`) can be assigned to characteristics.
* Outputs can use either split-free ring buffers (`RINGBUF_TYPE_NOSPLIT`) or byte ring buffers (`RINGBUF_TYPE_BYTEBUF`).
* Transmissions support automatic packet header packaging (`auto_pack`) and priority scheduling levels to prioritize high-priority alerts over diagnostic debug prints.

### Thread-Safe RX Queuing
When data is written by a peer to a characteristic:
1. The NimBLE callback writes the payload directly into the characteristic's dedicated RX ring buffer.
2. The manager signals the registered FreeRTOS binary semaphore (`on_rx_semaphore`).
3. The client application wakes up, reads the data using `sys_ble_char_rx_dequeue(...)`, and processes it.

---

## Core Data Structures

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
  sys_ble_char_cfg_t info; // Properties (UUID, read/write/notify flags, descriptor name)
  size_t rx_buffer_size;   // Dedicated RX buffer allocation size
} sys_ble_char_create_t;
```

### TX Buffer Configuration (sys_ble_tx_buf_cfg_t)
```c
typedef struct {
  uint8_t buffer_id;           // Unique buffer identifier (0 to 3)
  RingbufferType_t buff_type;  // RINGBUF_TYPE_NOSPLIT or RINGBUF_TYPE_BYTEBUF
  size_t size;                 // Queue memory capacity (bytes)
  bool auto_pack;              // Automatic item fragmentation/packaging
  uint8_t header;              // Optional standard packet header ID
  size_t item_size;            // Size of each transmission item (bytes) / only for autopack
  uint8_t priority;            // Queue priority level (higher level sent first)
  bool is_indication;          // Notify (false) or Indicate (true) transfer type
} sys_ble_tx_buf_cfg_t;
```

---

## API Functions

### Initialization & Lifecycle APIs
* **`sys_ble_init(callbacks)`**:
  Initializes the underlying controller, registers connection/disconnection callbacks, and spawns the background BLE worker threads.
* **`sys_ble_database_sync()`**:
  Compiles the temporary GATT services and characteristics database definitions and registers them live to the active NimBLE stack. Must be called after changing GATT layouts.
* **`sys_ble_get_status(out_status)`**:
  Fetches MTU size, active connection state, RX overflow count telemetry, and the last error state.

### GATT Service APIs
* **`sys_ble_service_create(cfg)`**:
  Declares a new service definition.
* **`sys_ble_service_remove(svc_uuid)`**:
  Removes a service definition (and its children characteristics) from the registry.

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
  Queues data for transmission as a notification or indication. Blocks for up to 100ms if `return_when_full` is false.
