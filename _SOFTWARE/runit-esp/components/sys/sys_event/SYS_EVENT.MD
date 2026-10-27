# sys_event — events

Component `components/sys/sys_event/`. One publisher, any number of listeners: a source (a device, the power manager) publishes what happened and doesn't know who listens. Two ways out of `sys_event_publish`: the **events connector** (only the NOTIFY events: the ones that matter to the user) and **C listeners** (inline or queued; target a device or a function). What a user wants done on an event, or sent to the app, is the VM's job: the `ON_EVENT` block (VM_BLOCKS.MD).

## Files

| File | Role |
|---|---|
| `sys_event.h` | `sys_event_t`, event IDs and flags, `SYS_EVENT_PUBLISH`, the EVENT contract (`ON_EVENT`), C API |
| `errors/sys_event_errors.h` | `ERR_SYS_EVENT_MAP`, the module's error tags (included by `sys_event.h`) |
| `errors/errors_event.json` | The error messages |
| `sys_event.c` | Subscription table, publish, notify, event task |
| `contract_event.json` | The EVENT contract descriptor (`ON_EVENT` and the `sys_event_t` fields) |
| `Kconfig` | Subscriptions, queue, task |
| `AGENTS.md` | Rules for changing this folder |

## Event

```c
typedef struct __attribute__((packed)) {  // 18 B
  uint16_t source;   // device ID
  uint16_t event;    // SYS_EVENT_ID(contract, n) = [contract u8][n u8], like ops
  uint8_t  channel;  // pin / channel, 0 when none
  uint8_t  flags;    // the events table's: SYS_EVENT_NOTIFY or 0
  int32_t  value;    // level, mV, mA ... per event
  uint64_t t_ms;     // set by sys_event_publish: the board's clock in ms (board_time_now_ms: UTC once a client set it, else the time since the start)
} sys_event_t;
```

- **Event IDs belong to contracts**: a contract lists its events next to its ops, in the table `P_EVENTS` of rows `X(P, NAME, n, flags)`, and `SYS_EVENTS_DECLARE(P)` (`sys_device.h`) makes the IDs from its `P_CONTRACT` (e.g. `IO_EVENTS` in `sys_io.h` → `IO_EV_FALLING` = `0x0101`, `IO_EV_FALLING_FLAGS`). The power manager has events but no ops, so it sets `PWRM_CONTRACT` itself. A device's own events are `0xFFxx`. n `0xFF` is never used (wildcard).
- **`SYS_EVENT_NOTIFY`** in the table = an event that matters to the user (over-current, a fault), not one that only drives code (an edge, an alert pin). `SYS_EVENT_PUBLISH(PFX, NAME, src, ch, val)` takes the flags from the table, so a publisher can't get them wrong.
- **No loop guard**: a listener that republishes (TCA6424A, TPS55289, INA3221 on their INT / alert pin) subscribes to one exact source and channel, and that pin's device exists before it (`sys_device_use`), so a chain of republishes is finite (pin of the ESP → TCA → TPS → power manager) and can't close into a cycle. Keep that rule for a new republisher.

### Events by contract

| Contract | Event | ID | Flags | Value |
|---|---|---|---|---|
| DEVICE (`0x00`) | `SUSPENDED` | `0x0000` | NOTIFY | 0; source `0xFFFF`: a sweep (every device) |
| | `RESUMED` | `0x0001` | NOTIFY | 0; source `0xFFFF`: a sweep |
| IO (`0x01`) | `RISING` | `0x0100` | 0 (code flow) | level |
| | `FALLING` | `0x0101` | 0 | |
| | `ADC_OUTSIDE` | `0x0102` | 0 | mV |
| | `ADC_INSIDE` | `0x0103` | 0 | mV |
| POWER_MONITOR (`0x04`) | `OCP_WARNING` | `0x0400` | NOTIFY | mA |
| | `OCP_CRITICAL` | `0x0401` | NOTIFY | mA |
| POWER_VREG (`0x05`) | `OVP` | `0x0500` | NOTIFY | mV |
| | `OCP` | `0x0501` | NOTIFY | mA |
| | `SCP` | `0x0502` | NOTIFY | 0 |
| | `READY` | `0x0503` | 0 (code flow) | mV |
| POWER_MANAGER (`0x06`) | `SOURCE_CHANGED`, `BATTERY_LOW`, `BATTERY_CRITICAL`, `BUDGET_EXCEEDED`, `INPUT_OVP`, `PD_FIXED` | `0x0600`–`0x0605` | NOTIFY | per event |
| HBRIDGE (`0x09`) | `OVERCURRENT` | `0x0900` | NOTIFY | mA |
| | `THERMAL_OR_UVLO` | `0x0901` | NOTIFY | mA |
| | `EXTERNAL` | `0x0902` | NOTIFY | mA |
| DC motor device (own, `0xFF`) | `STALL` | `0xFF00` | NOTIFY | mA (the motor stopped itself) |

Publishers: ESP32-S3 GPIO (IO edges from its ISR, ADC events from its ADC task), TCA6424A (IO edges of its input port), ADS7128 (ADC window events), TPS55289 (VREG), INA3221 (PMON), DRV8962 (HBRIDGE), the DC motor (its own STALL), the power manager (POWER_MANAGER) and runit for the registry (DEVICE: the state listener of SYS_DEVICE.MD "Suspended and resumed"; its table `SYS_DEVICE_EVENTS` and its IDs `SYS_DEVICE_EV_*` are in `sys_device.h`, with `SYS_EVENT_NOTIFY`, so the registry layer below sys_event declares them).

## Publishing

`sys_event_publish(&ev)`:

1. `t_ms` is set from the board's clock (TIME.MD), the same time as an error record's stamp. In an ISR the clock can't be read (it takes a lock): the event is queued with the uptime, and the event task turns it into the clock's time before anything else sees it.
2. **NOTIFY → events connector** (connector `SYS_DATA_CONNECTOR_EVENTS` = `0x06`), first: whatever the listeners do next (an action stopping the board), the cause is already out. Frame in the command shape (like the VM telemetry, VM_CORE.MD): `[ROUTER_KEY_EVENT 0x07][SYS_EVENT_OP_NOTIFY 0x01]` and the event itself, `sys_event_t` (18 B: `[source u16][event u16][channel u8][flags u8][value i32][t_ms u64]`). Key 0x07 has no router: it only names the frame, so an app decoder reads it with its `[key][op][struct]` table. It has no dir byte: it is an outbound frame, not a request. The op is published (`event_op` in `enums.json`) and the struct is the EVENT contract's (`contract_event.json`, the arguments of ON_EVENT); the app decodes it in `runit-app/src/backend/protocol/events.ts`. A full connector queue drops it silently (like the errors stream).
3. The device listeners, inline, table order.
4. One copy queued if a function listener matches (`ERR_EVENT_QUEUE_FULL` otherwise).

**`sys_event_publish_deferred(&ev)`** defers everything the same way, from a task: for background tasks an op may wait for (ESP32-S3 GPIO's ADC task), so no listener ever runs in them (SYS_DEVICE.MD "Concurrency"). Queue full → `ERR_EVENT_QUEUE_FULL` to the caller. What the event task still has to do with a queued event (everything, or only the function listeners) is the queue item's, never the event's `flags`: the app sees exactly what the events table says.

**From an ISR** everything is deferred: the event is queued and the task does notify, inline, then queued. An ISR never gets an error; a drop is counted and reported by the task (`ERR_EVENT_QUEUE_FULL {source 0xFFFF, dropped n}`).

Listener errors are wrapped in `ERR_EVENT_DELIVERY {source, event, channel, device}` and reported; the other listeners still run; nothing returns to the publisher.

## C listeners

```c
uint8_t h;
// a device on the TCA6424A ALERT, GPIO 21 of the ESP: its ON_EVENT, inline
ERR_TRY(sys_event_subscribe_device(0, IO_EV_FALLING, 21, c->id, &h));
// a module that lives forever: fn(ev, ctx) in the event task
ERR_TRY(sys_event_subscribe_fn(SYS_EVENT_ANY_SOURCE, SYS_EVENT_ANY_EVENT, SYS_EVENT_ANY_CHANNEL, on_event, NULL, &h));
/* teardown: ERR_TRY(sys_event_unsubscribe(h)); */
```

- **Match**: `source` exact or `SYS_EVENT_ANY_SOURCE` (0xFFFF); `event` exact, `SYS_EVENT_ANY_EVENT` (0xFFFF) or `(contract << 8) | SYS_EVENT_ANY_IN` (any event of a contract); `channel` exact or `SYS_EVENT_ANY_CHANNEL` (0xFF).
- **Match function**: `sys_event_matches(source, event, channel, &ev)` is the rule above as a function, for code that filters events itself (the VM's `ON_EVENT` block); the subscription table uses the same one.
- **Where it runs** follows from what the listener is, not from a caller's choice, so it can't run where it would deadlock. **A device** (`sys_event_subscribe_device`) runs **inline**, in the publisher's task before publish returns: the registry lock is recursive, so this is safe in any publisher, a device op included. It may block on I/O (I2C), never on a delay or timeout.
- **A function** (`sys_event_subscribe_fn`) runs in the **event task** (`CONFIG_SYS_EVENT_TASK_PRIO`, default 6: above router work, below urgent): a module with locks of its own (the power manager: its lock, then the registry's) would take them inside a device op, against that order.
- **Device delivery**: `sys_device_write(device, EVENT_OP_ON_EVENT, &ev, sizeof(ev))`. The EVENT contract (`SYS_DEVICE_CONTRACT_EVENT` = 0x03) has one op, `EVENT_OP_ON_EVENT` = 0x0300, a write, in the one-row table `EVENT_OPERATIONS`; `OP_WRITE(EVENT, ON_EVENT)` / `OP_ENTRY_W(EVENT, ON_EVENT)`, args = `sys_event_t`. A removed device answers `ERR_DEV_NOT_FOUND`: no dangling context, and the registry mutex keeps a delivery and REMOVE apart.
- **Function delivery**: `fn(ev, ctx)`. Only for modules that live forever (the VM, the power manager). The matching slot is copied out under a spinlock and the function is called without it, so an unsubscribe doesn't wait for a delivery in progress.
- `CONFIG_SYS_EVENT_MAX_SUBS` (32, up to 255) slots; the handle is the slot; `ERR_EVENT_TABLE_FULL` when none is free.

## The VM

The VM subscribes once to every event (a function listener on every event, `vm_event_init`, booted after `sys_event_init`). It copies them into a per-pass buffer; an `ON_EVENT` block matches that buffer with `sys_event_matches` (VM_BLOCKS.MD, VM_CORE.MD). A program therefore reacts to any event, NOTIFY or not.

## The app

The app gets exactly the NOTIFY events, nothing to configure: the events tables decide which events matter to the user (a fault, an over-current), not the app. An event without NOTIFY (a pin edge, an alert pin) reaches the app only if a program sends it (VM: `ON_EVENT` → a send block). There is no filter or watch table on the device.

## Remote

None. The app gets the NOTIFY events on the events stream and nothing else; there is no router and no op. Key 0x07 only names the frame.

## Limits

- **No repeat suppression**: every NOTIFY publish is sent, so a flapping fault produces a record per occurrence (same as the errors stream).
- **Queue**: `CONFIG_SYS_EVENT_QUEUE_LEN` (16) events waiting for the event task. A full queue fails a publish with `ERR_EVENT_QUEUE_FULL`, and drops an ISR event (counted, reported by the task).
- **Connector queue**: a full events connector drops the NOTIFY frame without an error.

## Errors (`ERR_MOD_SYS_EVENT` = 9)

`ERR_EVENT_QUEUE_FULL {source, event, dropped}` LOW, `_TABLE_FULL` MEDIUM, `_NOT_FOUND {id}` MEDIUM, `_BAD_ARG` HIGH, `_DELIVERY {source, event, channel, device}` NONE (context).
