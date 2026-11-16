# sys_event — events

Component `components/sys/sys_event/`. One publisher, any number of listeners: a source publishes what happened and doesn't know who listens. The layer below the device registry: it knows no device, only numbers (a tag, a source, a channel) and C functions. It has no receiver of its own: what goes to the app or to another board is a listener like any other.

## Files

| File | Role |
|---|---|
| `sys_event.h` | The event (`sys_event_key_t`, `sys_event_t`), tags and flags, the events tables (`SYS_EVENTS_DECLARE`), the C API |
| `sys_event_modules.h` | Who owns which module of a tag; how a module that is not a device gives and claims its ID |
| `sys_event.c` | The subscription table, publish, hold and release, the lanes' rings and tasks |
| `errors/sys_event_errors.h` | `ERR_SYS_EVENT_MAP`, the module's error tags (included by `sys_event.h`) |
| `errors/errors_event.json` | The error messages |
| `Kconfig` | Subscriptions, largest payload, each lane's queue and task |
| `CMakeLists.txt` | The component: requires `errors`; privately `utils`, `time`, `freertos` |
| `AGENTS.md` | Rules for changing this folder |

## The event

```c
typedef struct __attribute__((packed)) {  // 8 B: what, who, where
  uint32_t tag;      // EVT_TAG(module, code)
  uint16_t source;   // the device, the screen, ...: what the module defines
  uint16_t channel;  // pin / channel / object on that source, 0 when it has none
} sys_event_key_t;

typedef struct __attribute__((packed)) {  // 20 B header, then the payload
  sys_event_key_t key;
  uint64_t t_ms;     // set by sys_event_publish: the board's clock in ms
  uint16_t flags;    // SYS_EVENT_NOTIFY (bit 0) and the lane (bits 4..7): the events table's
  uint16_t len;      // payload bytes, at most SYS_EVENT_DATA_MAX
  uint8_t data[];    // the payload: the evt_data_<NAME>_t of its table row
} sys_event_t;
```

- Packed and without pointers: it is copied into the queue, passed to the listeners and sent as it is. Every field lies at a multiple of its size up to 4 B and the payload at 20, so an event at a 4 B boundary is read without unaligned access; `sys_event` keeps it there (the queue, a hold, the stack).
- `t_ms` is the board's clock (`board_time_now_ms`: UTC once a client set it, else the time since the start), the stamp of an error record.
- The largest payload is `CONFIG_SYS_EVENT_DATA_MAX` (64 B); a larger one in an events table fails to compile.

### Tags

A tag is `[module u16][code u16]`, `EVT_TAG(module, code)`. The code is 1..`0xFFFE`: 0 is not used, so tag 0 is "no event", and `0xFFFF` is "any code". The module says who publishes (`sys_event_modules.h`):

| Module | Owner | Where it is assigned |
|---|---|---|
| `0x0001`..`0x00FE` | A module, a device contract included: its number (`MODULE_ID_<NAME>`, `module_id.h`), the same as its router key and the module of its errors | `MODULE_ID_DECLARE(NAME, 0x22)` in the module's header |
| `0x8000`..`0xFFFE` | A device type's own events | `SYS_EVENT_MODULE_TYPE(NAME_TYPE)`, in the driver's `.c`: the ID its `SYS_DEVICE_TYPE_DECLARE` gives |
| `0xFFFF` | The filters (`SYS_EVENT_ANY_TAG`, `SYS_EVENT_ANY_NOTIFY`) | Never a module |

`0x0000` and `0x00FF` are no module: a device's own events are its type's. There is no list of modules: a module gives its own number, two hex digits, the same in the declare and the define (`module_id.h`). Two modules with one number fail to link and already fail to compile where both headers meet.

### Events tables

A module lists its events once, in the format of every table (`cmd_table.h`): `P_EVENTS(X, P)` of rows `X(P, NAME, code, flags, { payload fields })`. The module of the tags and their lane ("Lanes") come from the table, never from a row, so no row can name another module's or go another way than its module's:

| Macro | Module | Lane | For |
|---|---|---|---|
| `SYS_EVENTS_DECLARE(P, LANE)` | `MODULE_ID_<P>`, after its `MODULE_ID_DECLARE` | `LANE` | A module's events, a contract's included (its ID is its module's number) |
| `SYS_DEVICE_OWN_EVENTS(P)` (`sys_device_driver.h`) | `SYS_EVENT_MODULE_TYPE(P_TYPE)`, after `SYS_DEVICE_TYPE_DECLARE(P, ...)` | `DEVICE` | A device type's own events, in its driver |

```c
#define IO_EVENTS(X, P)                       \
  X(P, RISING,     0x01, 0, {})               \
  X(P, ADC_INSIDE, 0x04, 0, { int32_t mV; })
SYS_EVENTS_DECLARE(IO, DEVICE)  // IO_EV_RISING = EVT_TAG(MODULE_ID_IO, 0x01), IO_EV_RISING_FLAGS, ...
```

- Both give the tag `P_EV_<NAME>`, its flags `P_EV_<NAME>_FLAGS` (the row's and the lane), the packed payload type `evt_data_P_EV_<NAME>_t`, the module `P_EV_MODULE` and the lane `P_EV_LANE`, and check at compile time the payload's size, the code's range (1..`0xFFFE`), the module's range (`0x0001`..`0xFFFE`) and that a row's flag is NOTIFY only.
- **`SYS_EVENT_NOTIFY`** in the table marks an event that matters to the user (an over-current, a fault), not one that only drives code (an edge, an alert pin). It is the only flag a row sets; with the table's lane it is all of the event's flags, and both come from the table, never from a call site.
- A contract's table is in the contract's header, a module's in its `*_events.h`, a device type's own in the driver's `.c`.

## Publishing

```c
ERR_TRY(SYS_EVENT_PUBLISH(IO_EV_FALLING, c->id, pin));                          // inline
err_report(SYS_EVENT_PUBLISH_DEFER(IO_EV_ADC_INSIDE, c->id, pin, .mV = mv));    // all of it in the lane's task
```

`SYS_EVENT_PUBLISH(NAME, source, channel, fields...)` takes the tag, the flags and the payload size from the table. Both macros call `sys_event_publish(key, flags, data, len)`, the one way in; a module that passes on an event it did not declare (another board's) calls it directly, with the event's flags: its lane goes with them.

What `sys_event_publish` does depends on where it is called:

| Caller | What happens |
|---|---|
| A task, no hold | `t_ms` stamped; the inline listeners run here, in table order; one copy is queued into the event's lane if a queued listener of that lane matches |
| A task that holds its events | `t_ms` stamped; the event is kept in the hold and delivered by `sys_event_release()`; with no room there it is deferred |
| `SYS_EVENT_DEFER` (`SYS_EVENT_PUBLISH_DEFER`) | `t_ms` stamped; all of it queued into its lane: the lane's task runs the inline listeners, then the queued ones |
| An ISR | Always deferred. The uptime is stamped (the clock takes a lock) and the lane's task turns it into the clock's time before a listener sees it |

- A publisher picks inline or `SYS_EVENT_DEFER` by what its task can bear: a task an op may wait for, or one with a deadline, defers.
- What the lane's task still has to do with a queued event is the queue item's, never the event's `flags`: a listener sees exactly what the events table says.
- Errors to the publisher: `ERR_EVENT_BAD_ARG` (a payload longer than `SYS_EVENT_DATA_MAX`, `NULL` data with a length, a lane that is not a row of `SYS_EVENT_LANES`), `ERR_EVENT_QUEUE_FULL` (the queued copy did not fit its lane). An ISR never gets an error: a drop there is counted per lane and that lane's task reports it (`ERR_EVENT_QUEUE_FULL {source 0xFFFF, event 0xFFFFFFFF, dropped n}`).
- A listener's failure is wrapped in `ERR_EVENT_DELIVERY {source, event, channel}` and reported; the other listeners still run; nothing returns to the publisher.

## Holding

No lock is recursive, and an inline listener runs in the publisher's task, so it must not run while that task holds a lock the listener may take. A task that publishes under such a lock holds its events and lets them go after it:

```c
uint32_t room[64];
sys_event_hold_t hold;
sys_event_hold(&hold, room, sizeof(room));
...                          // lock, publish, unlock
sys_event_release(&hold);    // the listeners run here
```

- While a task holds, its publishes without `SYS_EVENT_DEFER` are kept in the buffer, stamped with the time of the publish, and not delivered. `sys_event_release()` delivers them in the order of the publishes, as `sys_event_publish` would have: the inline listeners, then one copy queued into the event's lane for its queued listeners.
- The room is the caller's, at a 4 B boundary. An event takes 4 B, its 20 B header and its payload, padded to 4 B. One that does not fit is deferred to its lane's task, never dropped here.
- A task has one hold. A hold inside another keeps nothing and its release does nothing, so the events go out when the outermost one is released. Only a pointer is kept per task.
- The hold is over before the first listener runs, so a listener may publish (delivered at once) or hold itself.
- A held publish returns no error: a full queue and a listener's failure are reported at the release.
- Not from an ISR.

The device registry holds around every call: an event published inside a device op reaches its inline listeners in the same task once the task's outermost `sys_device_call` is over, not while the op runs (`CONFIG_SYS_DEVICE_EVENT_HOLD_BYTES`).

## Listeners

```c
uint16_t h;
ERR_TRY(sys_event_subscribe((sys_event_key_t){.tag = IO_EV_FALLING, .source = id, .channel = 21},
                            on_edge, NULL, SYS_EVENT_RUN_INLINE, &h));
/* teardown: ERR_TRY(sys_event_unsubscribe(h)); */
```

A listener is a function `err_h fn(const sys_event_t* event, void* ctx)` and a context. The event is borrowed, at a 4 B boundary; `SYS_EVENT_DATA(event, NAME)` is the typed view of its payload, after a check of `event->key.tag`.

**Filter**: a `sys_event_key_t`, each field exact or "any".

| Field | "Any" |
|---|---|
| `source` | `SYS_EVENT_ANY_SOURCE` (`0xFFFF`) |
| `channel` | `SYS_EVENT_ANY_CHANNEL` (`0xFFFF`) |
| `tag` | `SYS_EVENT_ANY_TAG` (any event), `EVT_TAG(module, SYS_EVENT_ANY_CODE)` (any event of a module), `SYS_EVENT_ANY_NOTIFY` (any event with `SYS_EVENT_NOTIFY`) |

`sys_event_match(&filter, event)` is the same rule as a function, for code that filters events itself; any context, an ISR too.

**Where it runs** (`sys_event_run_e`):

| Kind | Where | For |
|---|---|---|
| `SYS_EVENT_RUN_INLINE` | The publisher's task (or at its release; the lane's task for a deferred event), before the queued ones | A chain that should run in one go (a pin, an expander, a regulator). It may block on I/O, never on a delay |
| `SYS_EVENT_RUN_FIRST` | Inline, before every other listener: the table's first slot, kept for one | The listener that sends the events to the app (`SYS_EVENT_ANY_NOTIFY`), so an action stopping the board can't hide its cause |
| `SYS_EVENT_RUN_QUEUED_<LANE>` (`SYS_EVENT_RUN_QUEUED_IN(lane)` in a loop) | The task of that lane, for that lane's events only | A module with locks of its own that an inline run would take out of order |

- The table has `CONFIG_SYS_EVENT_MAX_SUBS` (32, up to 255) slots. `ERR_EVENT_TABLE_FULL` when none is free, and for `SYS_EVENT_RUN_FIRST` when the first slot has its listener.
- **Handle**: `[generation u8][slot u8]`. The generation counts the listeners the slot has had (1..255, then 1 again; never 0) and stays when the slot is freed, so the next listener of the slot gets another handle. A handle of a listener that is gone names no listener: `sys_event_unsubscribe` answers `ERR_EVENT_NOT_FOUND` and leaves the slot's new listener alone. Handle 0 is never a listener, so a zeroed field is "not subscribed".
- A matching slot is copied out under a critical section and the function is called without it, one slot at a time, so an unsubscribe doesn't wait for a delivery in progress. A listener may still get an event already being delivered after its unsubscribe.
- **A queued listener runs in one task**: the subscription names its lane, and it hears only the events of that lane, so it is never called by two tasks at once. A filter that spans lanes is subscribed once per lane (`SYS_EVENT_RUN_QUEUED_IN(lane)`), each with its handle. An inline listener runs in whatever task publishes, so several tasks may call it at once.
- **Context**: it must stay valid until the unsubscribe and a delivery in flight are over. What can go away is passed as an ID, not a pointer, and looked up at each delivery: `sys_device_event_subscribe` / `sys_device_event_subscribe_op` ([`SYS_DEVICE_EVENT.MD`](../sys_device_event/SYS_DEVICE_EVENT.MD)) does it for a device, whose `ON_EVENT` (or own event op) then gets the event.
- **No loop guard**: a listener that republishes subscribes to one exact source and channel of a device that exists before it (`sys_device_use`), so a chain of republishes is finite and can't close into a cycle.

## Lanes

The deferred events and the queued listeners go through **lanes**: each lane is a ring buffer and a task of its own, so a flood or a slow listener in one lane never holds up another. `SYS_EVENT_LANES(X)` (`sys_event.h`) is the list, one row per lane, and each row has its three Kconfig options:

| Lane | Events | Task priority | Queued listeners |
|---|---|---|---|
| `SYS` | The system's: `SESSION`, `PWRM` | `CONFIG_SYS_EVENT_SYS_TASK_PRIO` (7) | – |
| `DEVICE` | The devices': the contracts' (`IO`, `HBRIDGE`, `PMON`, `VREG`, `UART`), the registry's (`SYS_DEVICE`), every type's own | `CONFIG_SYS_EVENT_DEVICE_TASK_PRIO` (6) | the power manager, `sys_remote` |

- **The lane is the table's** (`SYS_EVENTS_DECLARE(P, LANE)`), so all the events of one module go one way and keep their order. Between lanes there is no order: whichever task gets there first delivers first; `t_ms` says when each happened.
- **It travels in the event**: bits 4..7 of `flags` (`SYS_EVENT_FLAG_LANE`, `SYS_EVENT_LANE_OF`). An event passed on (another board's: `sys_remote`, `sys_event_stream`) keeps the lane its board gave it.
- **One task per lane**: `sys_event_init()` (boot) starts them all. A task takes one item from its ring, copies it (so the room is free again before a slow listener runs), turns an ISR's uptime into the clock's time, reports its lane's ISR drops not reported yet, then runs the inline listeners of a deferred event and the queued listeners of its lane.
- In one lane a slow listener still holds up the events behind it.
- `CONFIG_SYS_EVENT_<LANE>_TASK_STACK` (4096). The SYS lane is above the DEVICE lane, so a flood of device events can't hold the system's back; both are above the router work task (5) and below the urgent task (15).
- **Adding a lane**: a row in `SYS_EVENT_LANES` and its `SYS_EVENT_<LANE>_QUEUE_BYTES`, `_TASK_PRIO`, `_TASK_STACK` in the Kconfig (and in `host/sys_event_host_config.h`); up to 16 lanes (4 bits of the flags).

## Limits

- **Queue**: `CONFIG_SYS_EVENT_<LANE>_QUEUE_BYTES` (1024 each) of ring buffer per lane, no-split. An item takes the buffer's 8 B, its own 4 B, the 20 B header and the payload. A full ring fails a task's publish with `ERR_EVENT_QUEUE_FULL` and drops an ISR's event (counted, reported by the lane's task); the other lanes go on.
- **No repeat suppression**: every publish is delivered, so a flapping fault is an event per occurrence.
- **Handles**: a slot's generation wraps after 255 listeners, so a handle kept across 255 later subscriptions of the same slot names the listener there again.

## Tests

Host tests in `host/` (`python test_host/run.py sys_event` from `runit-esp/`, `HOST_TESTS.MD`): the real `sys_event.c` and `errors` on the PC, the lanes' tasks as threads, an ISR as a thread that says it is one. `test_sys_event.c`: an events table's module, tags, flags and payload sizes; a table's lane in its flags; an inline listener before publish returns, with the clock, the table's flags and the payload; every filter field exact or any, `SYS_EVENT_ANY_NOTIFY`; a payload too long or missing, an unknown lane; a failing listener reported as `ERR_EVENT_DELIVERY` while the others run; `SYS_EVENT_RUN_FIRST` (its slot, its order, one only); a full table, an unknown run; handles (0, removed, a slot taken again, the generation back to 1); a hold (kept until the release, in order, the publish's time, payloads of odd sizes), a hold inside a hold, a listener publishing during the release; the queue filling without the tasks, a held copy that does not fit reported, the other lane still taking events; with the tasks: a queued listener hearing only its lane, in its lane's task; the queued listeners' copy (held events too), `SYS_EVENT_DEFER`, an event too big for its hold, an ISR's publish and drop, two tasks' holds apart.

Not covered on the host: priorities, the lanes' stacks, an interrupt's timing.

## Errors (`ERR_MOD_SYS_EVENT` = 0x25, `MODULE_ID_SYS_EVENT`)

| Tag | Severity | Fields | When |
|---|---|---|---|
| `ERR_EVENT_QUEUE_FULL` | LOW | `source`, `event`, `dropped` | The queued copy did not fit; ISR drops (source and event "any") |
| `ERR_EVENT_TABLE_FULL` | MEDIUM | | No free subscription slot |
| `ERR_EVENT_NOT_FOUND` | MEDIUM | `id` (the handle) | Unsubscribe of 0, of a handle never given or of a listener removed already |
| `ERR_EVENT_BAD_ARG` | HIGH | | A `NULL` function or handle, an unknown run kind, a payload too long |
| `ERR_EVENT_DELIVERY` | NONE (context) | `source`, `event`, `channel` | Wraps a listener's failure |

The listener that sends the `SYS_EVENT_NOTIFY` events to the app (`SYS_EVENT_RUN_FIRST`) is not in this component: the module that owns the link to the app subscribes it.
