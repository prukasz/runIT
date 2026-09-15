# System Callback Manager

The callbacks component copies events into a static FreeRTOS queue and dispatches them from one worker task.

## Event metadata

```c
typedef struct sys_callback_head_t {
  uint16_t callback_type;
  uint16_t route_mask;
  uint8_t static_action_id;
  uint8_t dynamic_action_id;
} sys_callback_head_t;
```

The route mask selects up to 16 system handlers. Static/system and dynamic/user action IDs are separate 8-bit values; zero disables that action. Payloads remain IO, power, BLE, or an internal own-function callback.

## Dispatch order

1. For CALLBACK_OWN_FUNC, invoke the driver function and finish. Routes and actions are bypassed.
2. Otherwise, invoke each registered handler selected by the route mask, in ascending slot order. The handler interprets the event itself. Unregistered slots are skipped.
3. Invoke the dynamic/user action, if nonzero.
4. Invoke the static/system action, if nonzero, even when the user action returns an error.
5. Reuse the worker's event storage for the next queued event.

Handlers receive a borrowed pointer valid only during dispatch. A handler retaining an event must copy it before returning; the VM route does this. Embedded pointers (including own-function device handles) are shallow-copied and their owners must maintain their lifetime until dispatch finishes.

## Configuration

Register route functions with sys_cb_register_route() at boot, before dispatch starts. Slots are IO 0, BLE 1, power 2, WiFi 3 (reserved), and VM 4. A NULL function clears a slot. Route registration overwrites the previous function.

sys_actions_init() registers sys_actions_invoke() using sys_cb_register_action_executor(). The hook accepts scope 0x00 for static actions and 0x01 for dynamic actions. A requested action with no executor reports ERR_BASE_INVALID_STATE. Registration is boot-time; event-source configurations select routes and action IDs during operation.

IO interrupt configuration holds static_action_id and dynamic_action_id. Power and BLE add_callback APIs accept route_mask, static_action_id, dynamic_action_id. SYS_IO_CB, SYS_PWR_CB, and SYS_BLE_CB propagate these values without interpreting them.

## Queue and execution

sys_callbacks_init() starts the static worker pinned to core 0 and reports failure if creation returns NULL. The queue exists before application startup.

sys_callback_trigger() copies one event and never waits for queue space, in either task or ISR context. It returns ERR_NULL_PTR for NULL input and ERR_BASE_NO_MEM if the queue is full. The ISR path uses xQueueSendFromISR and yields when required. The producer macros currently discard this return value.

Non-blocking posting prevents a callback or action from deadlocking when it posts into its own full queue. Handlers and actions still execute synchronously: long operations delay later events. Producers must stop using device handles before freeing them; queue copies do not extend object lifetimes.

Configuration remains in ble/Kconfig: queue length, worker stack size, priority, and route count (1–16). Error ownership currently uses OWNER_SYS_DEVICE_BASE.

## Compatibility

The previous 64-bit action mask is removed. Callers must supply two action IDs instead of a mask. Structures containing callback configuration have changed layout and must be rebuilt together. Existing serialized configurations using the old layout require migration.
