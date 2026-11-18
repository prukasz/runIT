# sys_event

Spec: `SYS_EVENT.MD` (this folder).

- Every change in this folder is followed by an edit of `SYS_EVENT.MD` and of this file, if it is needed.
- `sys_event` is below the device registry: it includes nothing of `sys_device` and names no device. What a device needs of events (its `ON_EVENT`, the subscription by ID) is `sys_device_event`.
- `sys_event_key_t` (8 B), `sys_event_t` (the 20 B header, then the payload), the tags in the events tables, `SYS_EVENT_NOTIFY`, the lane's bits of the flags (4..7) and the lanes' numbers, the "any" values of a filter and the error tags are wire format or read by the app: do not renumber them or change the layout. Every field of the event stays at a multiple of its size and the payload at 20.
- A tag's module has one owner (`sys_event_modules.h`): a module's number (`MODULE_ID_DECLARE` in its `<module>_id.h`, the same number as its router key and the module of its errors; a contract's ID is one) or `SYS_EVENT_MODULE_TYPE(type ID)`. Never add a list of modules here: `sys_event` names no module above it. Code 0 and code `0xFFFF` are never an event.
- An event is declared in an events table `P_EVENTS(X, P)` (`SYS_EVENTS_DECLARE`, a driver's own `SYS_DEVICE_OWN_EVENTS`), whose module comes from the prefix and whose lane from the declare (`SYS_EVENTS_DECLARE(P, MODULE_ID_<NAME>, LANE)`; a type's own: DEVICE): a row never names a module or a lane, nor writes `EVT_TAG` itself. It is published with `SYS_EVENT_PUBLISH` / `SYS_EVENT_PUBLISH_DEFER`, which take its tag, flags and payload size from the table. `SYS_EVENT_NOTIFY` is set in the table, never at a call site, and only for events the user should see.
- `SYS_EVENT_DEFER` and the queue item's `todo` say how one publish is delivered: they never go into `sys_event_t.flags`.
- No lock is recursive: an inline listener never runs while its publisher's task holds a lock the listener may take. Code that publishes under such a lock holds its events (`sys_event_hold`) before the lock and releases them after it. Only a pointer is kept per task (`s_hold`): the room for the held events is the caller's stack.
- An event is never dropped silently in a task: one that does not fit a hold is deferred, one that does not fit the queue is an error to the publisher or a report. Only an ISR drops, and its drops are counted per lane and reported by that lane's task.
- A lane is a row of `SYS_EVENT_LANES` and its three Kconfig options (`SYS_EVENT_<LANE>_QUEUE_BYTES`, `_TASK_PRIO`, `_TASK_STACK`, also in `host/sys_event_host_config.h`); nothing else names a lane by number. A queued listener hears one lane (`SYS_EVENT_RUN_QUEUED_<LANE>`): never add a run kind that hears several, a filter that spans lanes is subscribed once per lane.
- The subscription table lock (`s_lock`) guards copies only: never call a listener, report an error or wait under it.
- Listener failures are reported as `ERR_EVENT_DELIVERY`, never returned to the publisher; the other listeners still run.
- A listener's context that can go away is an ID, not a pointer.
- A subscription handle is `[generation u8][slot u8]`, never 0. A slot's generation is kept when the slot is freed: never clear a whole slot.
- The listener that sends the `SYS_EVENT_NOTIFY` events to the app is another module's: nothing here knows a stream, a router or the app.
- A republisher subscribes to one exact source and channel of a device that exists before it (`sys_device_use`). There is no loop guard.
- Changing `sys_event_t`, an events table or the error map requires an update in `templates/`: run `sh templates/sync.sh`.
- Host tests: `host/` (SYS_EVENT.MD "Tests"). A change here is run with `python test_host/run.py sys_event` from `runit-esp/`. The tests before the queue's run without the lanes' tasks; the tasks are started once and stay, so a test after it waits for what a task delivers (a latch or a count), and drains what an earlier one left in the SYS lane first.
