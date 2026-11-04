# sys_remote

Spec: `SYS_REMOTE.MD` (this folder).

- Every change in this folder is followed by an edit of this file, if it is needed.
- Nothing depends on this component: it uses the session, the watches, the events and sys_device, and only `runit` calls it (boot, its router, the error sink's `sys_remote_on_error`). Keep it so; sys_device knows the hook (`sys_device_remote_ops_t`), never this component.
- The hook runs without the registry lock (a caller's own lock may be held, through a recursive take, when a remote device is a `pin_ref`): it may wait (a `WIRED` device on a wired way only) but never takes a lock that a task holding the registry lock waits for.
- `added` / `removed` run under the registry lock: they take a slot and send a call, and never wait.
- A write's copy is made old (`sys_watch_stale`) when its answer comes, never before: the refresh read must be after the write.
- The event function and `sys_remote_on_error` only queue (a reporter may be a timer's task); a frame that no queue took still counts in its subscriptions' `seq`. What comes in on the channel is queued on the RX task and published and reported from the component's task, never on the RX task.
- The tables' mutex guards copies only: never call the session, `sys_event` or a report under it.
- `config/sys_remote_config.h` (the frame's kinds) and the router's codes are wire format: never renumber them. Its error tags are in `errors/` (`ERR_MOD_SYS_REMOTE` = 21; code 1 is unused); a new tag gets its message in `errors/errors_sys_remote.json` in the same change, then `sh templates/sync.sh`. The registration is a constructor nothing references: keep `WHOLE_ARCHIVE` in `CMakeLists.txt`.
