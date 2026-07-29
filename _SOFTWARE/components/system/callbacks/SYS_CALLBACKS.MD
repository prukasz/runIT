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
2. The event is posted via `sys_callback_trigger(event)`. If called from an ISR context (`xPortInIsrContext()`), it routes through `xQueueSendFromISR` with high-priority task unblocking (`portYIELD_FROM_ISR`); otherwise it blocks on `xQueueSend` up to `portMAX_DELAY`. Returns `ERR_INVALID_VAL_UI32` for a NULL event, or `ERR_BASE_NO_MEM` if the send itself fails (queue full). The queue (`s_callback_queue`) is defined via the `utils` component's `R_QUEUE_DEFINE` — it's constructor-created and live before `app_main()` runs, so a trigger fired before `sys_callbacks_init()` has started the consumer task simply waits in the queue rather than being rejected.
3. The static background task (`sys_cb_task`, created via the `utils` component's `R_TASK_DEFINE` / `R_TASK_START_ON_CORE` wrappers) dequeues events sequentially via `R_QUEUE_RECEIVE(..., WAIT_FOREVER)`.

**`CALLBACK_OWN_FUNC` bypasses routing entirely**: the task invokes `event.event.own_func.own_func(device_handle, &event)` directly and moves on, unaffected by `route_mask`. This is what `sys_ble`'s callback-mode RX delivery builds on — see [[SYS_BLE.MD]].

**Every other event type goes through the static route table** (`s_route_table[SYS_CB_ROUTE_COUNT]` in sys_callbacks.c): for bit `i` set in `event.head.route_mask`, `sys_cb_task` calls `s_route_table[i](&event)` if a handler is registered there, silently skipping unset bits and empty slots. The index constants live in `sys_callbacks.h` so callers building a `route_mask` (e.g. the `route_mask` argument threaded through `sys_vreg_add_callback()`, `sys_ble_add_callback()`, `SYS_IO_CB()`) can target specific routes with `SYS_CB_ROUTE_BIT(idx)`:

| Index | Macro | Handler | Status |
| :--- | :--- | :--- | :--- |
| 0 | `SYS_CB_ROUTE_LOGGER` | `sys_cb_route_logger` | Implemented — `ESP_LOGI`s `IO`/`PWR`/`BLE` payload fields (the old skeleton logic, unchanged in behavior, moved here) |
| 1 | `SYS_CB_ROUTE_BLE` | `sys_cb_route_ble` | Registered stub — body is a `TODO`, no routing logic yet |
| 2 | `SYS_CB_ROUTE_WIFI` | — | Reserved, not implemented — no entry in `s_route_table`, WiFi doesn't exist yet |
| 3–15 | — | — | Unassigned — free for progressive expansion |

`route_mask` is a `uint16_t` (`SYS_CB_ROUTE_COUNT` = 16), so index range is 0–15. A caller that never sets any route bit (`route_mask == 0`) gets no dispatch at all for that event — this is opt-in per event source, not a broadcast default.

Every error raised inside `sys_callbacks.c` is tagged `OWNER_SYS_DEVICE_BASE` — `callbacks` has no dedicated owner map of its own yet, so it currently borrows this owner id from [[SYS_DEVICE.MD]] rather than defining `OWNER_SYS_CALLBACKS_*` tags.

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
  CALLBACK_MAX
} callback_type_e;
```

### Callback Header (sys_callback_head_t)
```c
typedef struct sys_callback_head_t {
  uint16_t callback_type; /* callback_type_e value */
  uint16_t route_mask;    /* Bitmask for destination routing */
} sys_callback_head_t;
```

### Route Table Indices (sys_callbacks.h)
```c
#define SYS_CB_ROUTE_LOGGER 0
#define SYS_CB_ROUTE_BLE    1
#define SYS_CB_ROUTE_WIFI   2
#define SYS_CB_ROUTE_COUNT  16
#define SYS_CB_ROUTE_BIT(idx) (1u << (idx))
```
See [Dynamic Callback Event Routing](#dynamic-callback-event-routing) above for how these map to `s_route_table`.

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

typedef struct own_func_t {
  err_h (*own_func)(void* device_handle, struct cb_event_t* event);
  void* device_handle;
} own_func_t;

typedef own_func_t own_funct_t;
```

### Unified Callback Event (cb_event_t)
```c
typedef struct cb_event_t {
  sys_callback_head_t head;
  union {
    io_event_t io;
    pwr_event_t pwr;
    ble_event_t ble;
    own_func_t own_func;
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

Both return `err_h` (`NULL` on success), per the [[runit]] skill's function return signature convention.

### Initialization & Queue Management
* **`err_h sys_callbacks_init(void)`**:
  The event queue (`CALLBACK_QUEUE_LEN = 16`, sized for `cb_event_t`) is already live by the time this is called — it's defined via `R_QUEUE_DEFINE`, constructor-created before `app_main()`. This function's only job is to start the consumer task (`sys_cb_task`, 4096-word stack, priority 5, pinned to core 0) via `R_TASK_START_ON_CORE`, guarded so a repeat call doesn't start it twice. Always returns `NULL`.

### Event Triggering
* **`err_h sys_callback_trigger(const cb_event_t* event)`**:
  Enqueues an event object into the callback processing queue. Safe to call from both task context and ISR context (see routing rule above).
