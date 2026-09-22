# Events (sys_event)

One publisher, any number of listeners. A source publishes what happened and
doesn't know who listens. Listeners live in one static subscription table.

API: `include/sys_event.h`. Errors: `errors/sys_error_event.h` (domain `0xAB`).

## Event

```c
typedef struct sys_event_t {
  uint8_t domain;    /* sys_event_domain_e */
  uint8_t device_id; /* source device (or feature) ID */
  uint8_t channel;   /* pin / channel, 0 when the source has one */
  uint8_t event;     /* domain-specific enum */
  int32_t value;
  uint8_t hops;      /* republish depth */
} sys_event_t;
```

| Domain | Publisher | channel | event | value |
|---|---|---|---|---|
| `IO` | IO adapters (`sys_io_publish`) | pin | `sys_io_intr_mode_e`: the edge (digital) or the armed window mode (analog) | level or mV |
| `POWER` | power adapters, power manager (`sys_power_publish`) | monitor channel (0 for a rail) | `sys_power_events_e` | mV / mA / % / source |
| `HBRIDGE` | bridge drivers (`sys_hbridge_publish`) | bridge channel | `sys_hbridge_fault_reason_e` | current in mA |
| `BLE` | BLE stack (`sys_ble_publish`) | 0 | `sys_ble_events_e` | handle / reason / error |
| `FEATURE` | features | 0 | per feature (motor: `sys_hbridge_fault_reason_e`) | per feature |

Board-level power events (battery, budget, source, input faults) come from the
power manager with `device_id = SYS_POWER_NO_DEVICE` (255), so only a
subscription with device `SYS_EVENT_ANY` gets them.

## Subscriptions

```c
uint8_t id;
SE_TRY(sys_event_subscribe(&(sys_event_subscription_t){
    .domain = SYS_EVENT_DOMAIN_HBRIDGE, .device_id = 20, .channel = 1, .event = SYS_EVENT_ANY,
    .inline_call = true, .handler = motor_on_fault, .ctx = hb}, &id));
/* later: SE_TRY(sys_event_unsubscribe(id, false)); */
```

- **Match:** each of domain / device / channel / event is exact or `SYS_EVENT_ANY` (255).
- **Inline** (`inline_call = true`): C `handler` only, called in the publisher's
  task before `sys_event_publish()` returns, in table order. It may block on I/O
  (an I2C read), but never on a delay or timeout. This is how a chip's alert pin
  is chained to its adapter (`sys_io_subscribe_pin()`).
- **Queued:** a C `handler` and/or routes and actions, run by the event task.
  Per listener: handler, routes (ascending slot), user action (scope 0x01), then
  system action (scope 0x00), even if the user action failed.
- **User subscriptions** (`user = true`) are the ones made by packets. Packets
  can make only queued route / action listeners (no handler). The unsubscribe
  packet can remove only user subscriptions; `SYS_EVENT_SUB_ALL_USER` (255)
  removes all of them. System subscriptions (devices, power manager, features)
  are removed only by their owner's C code.
- The table holds `CONFIG_SYS_EVENT_MAX_SUBSCRIPTIONS` (default 32, about 20 B
  each); the ID is the slot. `ERR_EVENT_TABLE_FULL` when full.
- Unsubscribing doesn't wait for a delivery already in progress, so a listener's
  context must outlive it until the owner stops the source.

Who subscribes today:

| Listener | Kind | Match |
|---|---|---|
| TCA6424A, ADS7128, TPS55289, INA3221 (x2), DRV8962 | inline, system | their alert pin (IO, device, pin, any) |
| Power manager | queued handler, system | POWER, any, any, any (filters rails, monitors and the input itself) |
| Motor feature | inline, system | HBRIDGE, its bridge, its channel, any; republishes as FEATURE |
| App (packets) | queued routes / actions, user | anything |

## Publishing

`sys_event_publish(&ev)`:

1. `hops >= CONFIG_SYS_EVENT_MAX_HOPS` (default 4): dropped, `ERR_EVENT_LOOP`.
2. Inline listeners run now.
3. If any queued listener matches, one copy is queued (`ERR_EVENT_QUEUE_FULL` when it doesn't fit).

From an ISR the whole delivery is deferred: the event is queued and the event
task runs the inline listeners, then the queued ones. An ISR drop is counted and
reported (`ERR_EVENT_QUEUE_FULL`) by the task on its next event.

A listener that republishes sets `.hops = SYS_EVENT_CAUSED_BY(cause)`, so a
chain of republishes (including a loop back to itself) stops at the limit. The
helpers `sys_io_publish`, `sys_power_publish` and `sys_hbridge_publish` take
`hops` as a parameter (0 for a first-hand event).

Listener errors are reported (`SE_REPORT`); they don't stop the other listeners
and aren't returned to the publisher.

## Routes and actions

Routes are registered at boot by runit (`sys_event_register_route`), up to
`CONFIG_SYS_EVENT_ROUTE_COUNT` (8, an 8-bit mask). Slots are assigned in
`utils/Kconfig` ("Event Route Slots"): `CONFIG_SYS_EVENT_ROUTE_VM` = 0, the VM's
per-scan-cycle event buffer (`vm_event_route`). A slot nobody registers is skipped.

The action executor is `sys_actions_invoke`, registered by runit
(`sys_event_register_action_executor`). An action with no executor raises
`ERR_EVENT_NO_EXECUTOR`.

## Arming a source

A subscription only listens. The source is armed separately, as device
configuration:

- IO: `sys_io_configure_intr(pin, {mode, debounce, adc window})`.
- Power monitor: `sys_power_monitor_set_alert(dev, channel, OCP_WARNING / OCP_CRITICAL, threshold_mA)`.
- Regulator and H-bridge faults are always armed; with no listener, a fault is published to nobody.

## Packets

Class `CONFIG_RX_PACKET_CLASS_SYS_EVENTS` (0x09), `codecs/decoders/dec_sys_events.h`:

| Packet | Payload | Response |
|---|---|---|
| `0x01` subscribe | domain, device_id, channel, event (255 = any), route_mask, static_action_id, dynamic_action_id | `subscription_id` |
| `0x02` unsubscribe | subscription_id (255 = all user subscriptions) | status only |

## Task and configuration

The event task is pinned to core 0 (`CONFIG_SYS_EVENT_TASK_PRIO` 5, stack
`CONFIG_SYS_EVENT_TASK_STACK_SIZE` 4096). The queue holds
`CONFIG_SYS_EVENT_QUEUE_LEN` (16) events of 16 B. Queue and table are static, so
publishing and subscribing work before `sys_event_init()` starts the task.
