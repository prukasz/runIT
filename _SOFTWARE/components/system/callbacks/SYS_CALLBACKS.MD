# System Callback Manager (callbacks)

IMPORTANT: If you modify the source files in this component (sys_callbacks.h or sys_callbacks.c), you must update this documentation to reflect the changes.

## Overview
The `callbacks` component provides a centralized, thread-safe, and ISR-safe event routing and execution pipeline for the runIT system architecture. It decouples high-speed physical interrupts (GPIO inputs, power alerts, BLE stack state changes) from system execution worker threads by enqueuing structured event notifications into a dedicated FreeRTOS static queue processed by a background worker task.

### Directory Map
```
components/system/callbacks/
├── CMakeLists.txt
├── SYS_CALLBACKS.MD     # This documentation
├── sys_callbacks.c      # Static queue setup, background dispatch task, and ISR triggers
└── include/
    └── sys_callbacks.h  # Event types, payload unions, macro helpers, and API declarations
```

---

## Architecture & Concept

Event handling follows a unified asynchronous event-queue pipeline:
```
Interrupt / Hardware Event -> sys_callback_trigger() -> Static FreeRTOS Queue -> sys_cb_task -> Event Dispatcher / User Handler
```

### Dynamic Callback Event Routing
1. Hardware drivers or system submodules construct a generic `cb_event_t` containing a header (`sys_callback_head_t`) and an event payload union (`io`, `pwr`, `ble`, or `own_func`).
2. The event is posted via `sys_callback_trigger(event)`. If called from an ISR context (`xPortInIsrContext()`), `sys_callback_trigger` automatically routes through `xQueueSendFromISR` with high-priority task unblocking (`portYIELD_FROM_ISR`).
3. The static background task (`sys_cb_task`) dequeues events sequentially and dispatches them based on the `callback_type`.

---

## Core Data Structures & Enums

### Callback Types (callback_type_e)
```c
typedef enum callback_type_e {
  CALLBACK_NONE     = 0,
  CALLBACK_IO       = 1,
  CALLBACK_PWR      = 2,
  CALLBACK_BLE      = 3,
  CALLBACK_OWN_FUNC = 4,
} callback_type_e;
```

### Callback Header (sys_callback_head_t)
```c
typedef struct sys_callback_t {
  uint16_t callback_type;
  struct {
    uint16_t route_mask : 16;
  } route_to;
} sys_callback_head_t;
```

### Submodule Event Payloads
```c
typedef struct io_event_t {
  uint8_t device_id;
  uint8_t pin_id;
  uint16_t trigger_event;
  int32_t trigger_value;  // ADC raw reading or digital state boolean
} io_event_t;

typedef struct pwr_event_t {
  uint8_t device_id;
  uint8_t channel_id;
  uint16_t trigger_event;
  int32_t trigger_value;  // Current (mA) or voltage (mV) reading
} pwr_event_t;

typedef struct ble_event_t {
  uint32_t event;
  int32_t value;
} ble_event_t;

typedef struct own_funct_t {
  status_rep_t (*own_func)(void* device_handle, struct cb_event_t* event);
  void* device_handle;
} own_funct_t;
```

### Unified Callback Event (cb_event_t)
```c
typedef struct cb_event_t {
  sys_callback_head_t head;
  union {
    io_event_t io;
    pwr_event_t pwr;
    own_funct_t own_func;
    ble_event_t ble;
  } event;
} cb_event_t;
```

---

## Utility Macros

### Custom Function Callback Wrapper
* **`SYS_CB_OWN(own_func_struct)`**: Constructs an inline `cb_event_t` with `CALLBACK_OWN_FUNC` type, populates its function pointer structure, and triggers the callback event pipeline immediately.
  ```c
  SYS_CB_OWN(my_own_func_struct);
  ```

---

## Manager API Functions

### Initialization & Queue Management
* **`sys_callbacks_init()`**:
  Allocates the static FreeRTOS event queue (`CALLBACK_QUEUE_LEN = 16`) and launches the dedicated `sys_cb_task` background worker thread.

### Event Triggering
* **`sys_callback_trigger(cb_event_t* event)`**:
  Enqueues an event object into the callback processing queue. Safe to call from both task context and ISR context.
