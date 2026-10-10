# sys_event — events (for agents)

Component `components/sys/sys_event/`. One publisher, any number of listeners: a source (a device, later the power manager) publishes what happened and doesn't know who listens. Three ways out of `sys_event_publish`: the **events connector** (events that matter to the user), **C listeners** (inline or queued; target a device or a function) and **rules** (remote: event → action). Replaces legacy `sys_event` (domain enum, u8 device IDs, route slots, static / dynamic action IDs, class `0x09`).

## Files

| File | Role |
|---|---|
| `sys_event.h` | `sys_event_t`, event IDs and tables, `SYS_EVENT_PUBLISH`, the EVENT contract (`ON_EVENT`), C API, rules API, error tags |
| `sys_event.c` | Table (subscriptions + rules), publish, notify, event task |
| `sys_event_router.c/.h` | `events` router (`ROUTER_KEY_EVENTS` = 0x07): rules, test publish |
| `Kconfig` | Subscriptions, rules, queue, hops, task |

## Event

```c
typedef struct __attribute__((packed)) {  // 15 B
  uint16_t source;   // device ID
  uint16_t event;    // SYS_EVENT_ID(contract, n) = [contract u8][n u8], like ops
  uint8_t  channel;  // pin / channel, 0 when none
  uint8_t  hops;     // republish depth
  uint8_t  flags;    // SYS_EVENT_NOTIFY
  int32_t  value;    // level, mV, mA ... per event
  uint32_t t_ms;     // set by sys_event_publish (esp_timer)
} sys_event_t;
```

- **Event IDs belong to contracts**: a contract lists its events next to its ops, `X(NAME, n, flags)` (`SYS_IO_EVENTS` in `sys_io.h` → `IO_EV_FALLING` = `0x0101`, `IO_EV_FALLING_FLAGS`). A device's own events are `0xFFxx`. No central domain enum. n `0xFF` is never used (wildcard).
- **`SYS_EVENT_NOTIFY`** in the table = an event that matters to the user (over-current, a fault), not one that only drives code (an edge, an alert pin). `SYS_EVENT_PUBLISH(PFX, NAME, src, ch, val, cause)` takes the flags from the table, so a publisher can't get them wrong.
- **Hops**: a listener that publishes because of an event sets `.hops = SYS_EVENT_CAUSED_BY(cause)` (`SYS_EVENT_PUBLISH` does); at `CONFIG_SYS_EVENT_MAX_HOPS` (4) the event is dropped with `ERR_EVENT_LOOP`.

| Contract | Events | Flags |
|---|---|---|
| IO (`0x01`) | `RISING` 0x0100 (value = level), `FALLING` 0x0101, `ADC_OUTSIDE` 0x0102 (value = mV), `ADC_INSIDE` 0x0103 | 0 (code flow) |

## Publishing

`sys_event_publish(&ev)`:

1. `hops >= MAX_HOPS` → `ERR_EVENT_LOOP`. `t_ms` is set.
2. **NOTIFY → events connector** (`SYS_CONNECTOR_EVENTS` = 3, stream `0x06`), first: whatever the listeners do next (an action stopping the board), the cause is already out. Record (13 B): `[source u16][event u16][channel u8][value i32][t_ms u32]`. A full connector queue drops it silently (like the errors stream).
3. Inline listeners, table order.
4. One copy queued if a queued listener or a rule matches (`ERR_EVENT_QUEUE_FULL` otherwise).

**`sys_event_post(&ev)`** (2026-10-10) defers everything the same way from a task: for background tasks an op may wait for (GPIO ESP's ADC task), so no listener ever runs in them (SYS_DEVICE.MD "Concurrency"). Queue full → `ERR_EVENT_QUEUE_FULL` to the caller.

**From an ISR** everything is deferred: the event is queued and the task does notify, inline, then queued. An ISR never gets an error; a drop is counted and reported by the task (`ERR_EVENT_QUEUE_FULL {source 0xFFFF, dropped n}`).

Listener errors are wrapped in `ERR_EVENT_DELIVERY {source, event, channel, rule, device}` and reported; the other listeners still run; nothing returns to the publisher.

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
- **Inline** (`inline_call`): in the publisher's task before publish returns. May block on I/O (I2C), never on a delay or timeout. **Queued**: the event task (prio 6: above router work, below urgent).
- **Target device** (`device` ≠ `SYS_DEVICE_ID_NONE`): `sys_device_call(device, EVENT_OP_ON_EVENT, &ev)` — the EVENT contract (`SYS_DEVICE_CONTRACT_EVENT` = 0x03, `EVENT_HANDLER(ON_EVENT)` / `EVENT_ENTRY(ON_EVENT)`, args = `sys_event_t`). A removed device answers `ERR_DEV_NOT_FOUND`: no dangling context, and the registry mutex keeps a delivery and REMOVE apart.
- **Target fn** (`device` = `SYS_DEVICE_ID_NONE`): `fn(ev, ctx)`. Only for modules that live forever (VM, power manager): the table is copied out under a spinlock and called without it, so an unsubscribe doesn't wait for a delivery in progress.
- `CONFIG_SYS_EVENT_MAX_SUBS` (32) slots; the handle is the slot; `ERR_EVENT_TABLE_FULL`.

## Rules (remote)

A rule = "event → action", made by the app, always queued. Its own table (`CONFIG_SYS_EVENT_MAX_RULES` 16), so rules never take a device's slot and a remote command can't touch a C listener.

- ID 1..254 chosen by the client; SUBSCRIBE with an existing ID replaces that rule (the stored project replays to the same IDs).
- Fired: `runner(action, urgent)` — the app sets the runner (`sys_event_set_action_runner(sys_actions_request)` in `main`), so `sys_event` doesn't know `sys_actions`. The action is injected as a RUN frame on the router's internal link: it runs on a router task, its errors go to the errors stream with link 0 and the request `06 01 <id>` (SYS_ACTIONS.MD). No runner → `ERR_EVENT_NO_RUNNER`.
- The event's value doesn't reach the action (actions are fixed frames).

## Remote (`sys_event_router`, root key `ROUTER_KEY_EVENTS` = 0x07)

`rule` = `{u8 id, u16 source, u16 event, u8 channel, u8 action, u8 flags}` (8 B), flags bit 0 = the action runs urgent.

| Op | Request | Response |
|---|---|---|
| `0x01` SUBSCRIBE | rule | – (same ID: replaced) |
| `0x02` UNSUBSCRIBE | `u8 id` (0xFF = every rule) | – |
| `0x03` LIST | `u8 start` | `u8 total`, then rules by ascending ID from `start`, as many as fit |
| `0x04` PUBLISH | `u16 source, u16 event, u8 channel, i32 value, u8 flags` | – (a first-hand event: tests a rule or the notify stream without hardware) |

Example — GPIO 4 of device 0 falls → action 1: `07 01 01 00 00 01 01 04 01 00`.

## Errors (`ERR_MOD_SYS_EVENT` = 9)

`ERR_EVENT_LOOP {source, event, hops}` MEDIUM, `_QUEUE_FULL {source, event, dropped}` LOW, `_TABLE_FULL` MEDIUM, `_NOT_FOUND {id}` MEDIUM, `_BAD_ARG` HIGH, `_BAD_ID {id}` HIGH (rule 0 / 0xFF), `_NO_RUNNER {action}` MEDIUM, `_DELIVERY {source, event, channel, rule, device}` NONE (context).

## Changes from legacy (`legacy/components/system/sys_event`)

- Event IDs per contract (op layout) instead of `sys_event_domain_e` + per-domain enums; u16 source.
- Device listeners by ID through the EVENT contract instead of raw `ctx` pointers.
- Rules: one action ID per rule, run through the router (link 0) instead of static + dynamic action IDs run in the event task; rule IDs chosen by the client instead of returned; own table.
- No route slots: the VM subscribes its own fn listener.
- Notify stream (new): `SYS_EVENT_NOTIFY` events on connector 3 / stream 0x06; user connectors moved to 4+.
- Class `0x09` → `events` router.

## Open

- **VM listeners (decide before the VM port):** a fn listener called inline from a device op runs under the device lock. If the VM holds its own mutex while calling devices and its inline listener waits for that mutex, the two deadlock. Either the VM's listeners are always queued, or the VM never calls a device while holding its own lock.

- Publishers (2026-10-10, not run on the board): GPIO ESP CONFIGURE_INTR (pin edges from the ISR, ADC thresholds from the ADC task) and TPS55289 (an inline device-target listener on its FB/INT pin that republishes `VREG_EV_*`). Not yet: the other chips with alert pins, the power manager. Only PUBLISH → rules / notify has been run (QEMU).
- A pin edge is code flow by default (flags 0); a user's button might deserve a notify — e.g. a notify bit in CONFIGURE_INTR, decided by whoever arms the pin.
- No repeat suppression on the notify stream (same open point as the errors stream).
