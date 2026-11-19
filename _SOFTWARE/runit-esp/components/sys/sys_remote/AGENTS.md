# sys_remote

Spec: `SYS_REMOTE.MD` (this folder).

- Every change in this folder is followed by an edit of this file, if it is needed.
- Nothing depends on this component: it uses the session, the watches, the events and sys_device, and only `app/runit` calls it (boot, the error sink's `sys_remote_on_error`). Keep it so; sys_device knows the hook (`sys_device_remote_f`), never this component.
- The hook runs without the registry lock (a caller's own lock may be held when a remote device is a `pin_ref`): it may wait (a `WIRED` device on a wired way only) but never takes a lock that a task holding the registry lock waits for.
- A write's copy is made old (`sys_watch_stale`) when its answer comes, never before: the refresh read must be after the write.
- The event function and `sys_remote_on_error` only queue (a reporter may be a timer's task). What comes in on the channel is queued on the RX task and published and reported from the component's task, never on the RX task.
- The table's mutex guards copies only: never call the session, `sys_event` or a report under it.
- There is no table of aliases: the registry is the one source of what a remote device is (`sys_device_find_remote`, `sys_device_remote_of`).
- `config/sys_remote_config.h` (the frame's kinds) and the router's codes are wire format: never renumber them. The router's op table and its frame are in `router/sys_remote_router.h`; every `struct` name in `contract_remote.json` is a C type of it. Its error tags are in `errors/`; a new tag gets its message in `errors/errors_sys_remote.json` in the same change. The registrations are constructors nothing references: keep `WHOLE_ARCHIVE` in `CMakeLists.txt`.
