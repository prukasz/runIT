# sys_event

Spec: `SYS_EVENT.MD` (this folder).

- Every change in this folder is followed by an edit of this file, if it is needed.
- `sys_event_t` (layout, 18 B), the event IDs in the contract tables, the flag values (`SYS_EVENT_NOTIFY`, `SYS_EVENT_DEFER`, `SYS_EVENT_FROM_ISR`), `SYS_EVENT_OP_NOTIFY`, `ROUTER_KEY_EVENT` (0x07) and the error tags are wire format or read by the app: do not renumber them or change the layout.
- Changing `sys_event_t`, the NOTIFY frame, `contract_event.json` or the error map requires an update in `templates/`: run `sh templates/sync.sh` (regenerates `enums.json` / `errors.json`, refreshes `runit-app/generated/`, validates). Commit with `--check` green.
- NOTIFY is set in the contract's events table, never by a publisher's call site. Only events the user should see get it.
- A task an op may wait for publishes with `SYS_EVENT_DEFER`; no caller holds a lock of its own across `sys_event_publish` (SYS_DEVICE.MD "Concurrency").
- The subscription table lock (`s_lock`) guards copies only: never call a listener or a device under it.
- Listener failures are reported as `ERR_EVENT_DELIVERY`, never returned to the publisher.
- A republisher subscribes to one exact source and channel of a device that exists (`sys_device_use`). There is no loop guard.
