# sys_event — events (for agents)

Component `components/sys/sys_event/`. One publisher, any number of listeners: a source (a device, later the power manager) publishes what happened and doesn't know who listens. Two ways out of `sys_event_publish`: the **events connector** (only the NOTIFY events: the ones that matter to the user) and **C listeners** (inline or queued; target a device or a function). What a user wants done on an event, or sent to the app, is the VM's job (`ON_EVENT` block): the remote rules (event → action) and watches (event → stream) were removed 2026-10-11. Replaces legacy `sys_event` (domain enum, u8 device IDs, route slots, static / dynamic action IDs, class `0x09`).

## Files

| File | Role |
|---|---|
| `sys_event.h` | `sys_event_t`, event IDs and tables, `SYS_EVENT_PUBLISH`, the EVENT contract (`ON_EVENT`), C API, error tags |
| `sys_event.c` | Subscription table, publish, notify, event task |
| `Kconfig` | Subscriptions, queue, task |

## Event

```c
typedef struct __attribute__((packed)) {  // 14 B
  uint16_t source;   // device ID
  uint16_t event;    // SYS_EVENT_ID(contract, n) = [contract u8][n u8], like ops
  uint8_t  channel;  // pin / channel, 0 when none
  uint8_t  flags;    // SYS_EVENT_NOTIFY
  int32_t  value;    // level, mV, mA ... per event
  uint32_t t_ms;     // set by sys_event_publish (esp_timer)
} sys_event_t;
```

- **Event IDs belong to contracts**: a contract lists its events next to its ops, `X(NAME, n, flags)` (`SYS_IO_EVENTS` in `sys_io.h` → `IO_EV_FALLING` = `0x0101`, `IO_EV_FALLING_FLAGS`). A device's own events are `0xFFxx`. No central domain enum. n `0xFF` is never used (wildcard).
- **`SYS_EVENT_NOTIFY`** in the table = an event that matters to the user (over-current, a fault), not one that only drives code (an edge, an alert pin). `SYS_EVENT_PUBLISH(PFX, NAME, src, ch, val)` takes the flags from the table, so a publisher can't get them wrong.
- **No loop guard** (hops removed 2026-10-11): a listener that republishes (TCA6424A, TPS55289, INA3221 on their INT / alert pin) subscribes to one exact source and channel, and that pin's device exists before it (`sys_device_use`), so a chain of republishes is finite (pin of the ESP → TCA → TPS → power manager) and can't close into a cycle. Keep that rule for a new republisher. The VM (queued) guards itself against reacting to what it published.

| Contract | Events | Flags |
|---|---|---|
| IO (`0x01`) | `RISING` 0x0100 (value = level), `FALLING` 0x0101, `ADC_OUTSIDE` 0x0102 (value = mV), `ADC_INSIDE` 0x0103 | 0 (code flow) |

## Publishing

`sys_event_publish(&ev)`:

1. `t_ms` is set.
2. **NOTIFY → events connector** (`SYS_CONNECTOR_EVENTS` = 3, stream `0x06`), first: whatever the listeners do next (an action stopping the board), the cause is already out. Frame in the command shape (like the VM telemetry, VM_CORE.MD): `[ROUTER_KEY_EVENT 0x07][SYS_EVENT_OP_NOTIFY 0x01]` and the event itself, `sys_event_t` (14 B, layout above: `[source u16][event u16][channel u8][flags u8][value i32][t_ms u32]`). Key 0x07 has no router: it only names the frame, so an app decoder reads it with its `[key][op][struct]` table. A full connector queue drops it silently (like the errors stream).
3. Inline listeners, table order.
4. One copy queued if a queued listener matches (`ERR_EVENT_QUEUE_FULL` otherwise).

**`SYS_EVENT_DEFER`** (flag 0x02, set by the publisher, 2026-10-11; replaces `sys_event_post`) defers everything the same way from a task: for background tasks an op may wait for (GPIO ESP's ADC task), so no listener ever runs in them (SYS_DEVICE.MD "Concurrency"). Queue full → `ERR_EVENT_QUEUE_FULL` to the caller.

**From an ISR** everything is deferred: the event is queued and the task does notify, inline, then queued. An ISR never gets an error; a drop is counted and reported by the task (`ERR_EVENT_QUEUE_FULL {source 0xFFFF, dropped n}`).

Listener errors are wrapped in `ERR_EVENT_DELIVERY {source, event, channel, device}` and reported; the other listeners still run; nothing returns to the publisher.

## C listeners

```c
uint8_t h;
ERR_TRY(sys_event_subscribe(&(sys_event_sub_t){
    .source = 0, .event = IO_EV_FALLING, .channel = 21,   // TCA6424A ALERT on GPIO 21 of the ESP
    .inline_call = true,
    .device = c->id,                                      // → its ON_EVENT
}, &h));
/* teardown: ERR_TRY(sys_event_unsubscribe(h)); */
```

- **Match**: `source` exact or `SYS_EVENT_ANY_SOURCE` (0xFFFF); `event` exact, `SYS_EVENT_ANY_EVENT` (0xFFFF) or `(contract << 8) | SYS_EVENT_ANY_IN` (any event of a contract); `channel` exact or `SYS_EVENT_ANY_CHANNEL` (0xFF).
- **Match function**: `sys_event_matches(source, event, channel, &event)` is the rule above as a function, for code that filters events itself (the VM's ON_EVENT block); the subscription table uses the same one.
- **Inline** (`inline_call`): in the publisher's task before publish returns. May block on I/O (I2C), never on a delay or timeout. **Queued**: the event task (prio 6: above router work, below urgent).
- **Target device** (`device` ≠ `SYS_DEVICE_ID_NONE`): `sys_device_call(device, EVENT_OP_ON_EVENT, &ev)` — the EVENT contract (`SYS_DEVICE_CONTRACT_EVENT` = 0x03, one op `EVENT_OP_ON_EVENT` = 0x0300, no X-macro table; `EVENT_HANDLER(ON_EVENT)` / `EVENT_ENTRY(ON_EVENT)`, args = `sys_event_t`). A removed device answers `ERR_DEV_NOT_FOUND`: no dangling context, and the registry mutex keeps a delivery and REMOVE apart.
- **Target fn** (`device` = `SYS_DEVICE_ID_NONE`): `fn(ev, ctx)`. Only for modules that live forever (VM, power manager): the table is copied out under a spinlock and called without it, so an unsubscribe doesn't wait for a delivery in progress.
- `CONFIG_SYS_EVENT_MAX_SUBS` (32) slots; the handle is the slot; `ERR_EVENT_TABLE_FULL`.

## The app

The app gets exactly the NOTIFY events, nothing to configure: the contract's events table decides which events matter to the user (a fault, an over-current), not the app. An event without NOTIFY (a pin edge, an alert pin) reaches the app only if a program sends it (VM: `ON_EVENT` → a send block). Watches (a remote "send me these events" filter table) were tried and removed 2026-10-11: a second path to the app next to the program, for the same need.

## Remote

None. The app gets the NOTIFY events on the events stream and nothing else; there is no router (the test PUBLISH op was removed 2026-10-11; key 0x07 only names the frame). The notify path is checked on the board with real faults.

## Errors (`ERR_MOD_SYS_EVENT` = 9)

`ERR_EVENT_QUEUE_FULL {source, event, dropped}` LOW, `_TABLE_FULL` MEDIUM, `_NOT_FOUND {id}` MEDIUM, `_BAD_ARG` HIGH, `_DELIVERY {source, event, channel, device}` NONE (context). Tags 1 (`LOOP`, hops), 6 (`BAD_ID`, watches) and 7 (`NO_RUNNER`, rules) are retired.

## Changes from legacy (`legacy/components/system/sys_event`)

- Event IDs per contract (op layout) instead of `sys_event_domain_e` + per-domain enums; u16 source.
- Device listeners by ID through the EVENT contract instead of raw `ctx` pointers.
- No remote actions: legacy subscriptions with static / dynamic action IDs (and the rules that briefly replaced them) are gone; the VM's `ON_EVENT` + `ACTION` blocks do that. No remote event config at all: the app gets the NOTIFY events.
- No route slots: the VM subscribes its own fn listener.
- Notify stream (new): `SYS_EVENT_NOTIFY` events on connector 3 / stream 0x06; user connectors moved to 4+.
- Class `0x09` (rules, subscriptions) gone with no router in its place.

## Open

- **VM (decided 2026-10-11, done at the VM port):** one queued fn subscription (any / any / any) whose fn is a pre-filter: an event enters the VM's per-pass buffer only if it matches one of the loaded program's `ON_EVENT` filters, with the same `matches()` the block uses; the block keeps its own query. Queued, so the VM never runs inside a device op (the deadlock with the device lock can't happen). `ON_EVENT` filter = `source u16 / event u16 / channel` (legacy: domain / u8 device). The power manager decides the system response to power events (SYS_POWER_MANAGER.MD); the VM does the user's logic where that response is NOTIFY.

- Publishers (not run on the board): GPIO ESP CONFIGURE_INTR (pin edges from the ISR, ADC thresholds from the ADC task), TPS55289, INA3221 and TCA6424A (inline device-target listeners on their INT / alert pins that republish), the power manager. Only PUBLISH → rules / notify has been run (QEMU, before the rules were removed).
- A pin edge is code flow (flags 0); an app that wants to see a button needs a program that sends it.
- No repeat suppression on the notify stream (same open point as the errors stream).
