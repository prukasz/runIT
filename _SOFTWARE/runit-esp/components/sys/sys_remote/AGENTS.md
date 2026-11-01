# sys_remote

Spec: `SYS_REMOTE.MD` (this folder).

- Every change in this folder is followed by an edit of this file, if it is needed.
- Nothing depends on this component: it uses the session, the watches, the connectors, the events and sys_device, and only `runit` calls it. Keep it so; sys_device knows the hook (`sys_device_remote_ops_t`), never this component.
- The hook runs without the registry lock (a caller's own lock may be held, through a recursive take, when a remote device is a `pin_ref`): it may wait (`blocking`, a wired neighbour only) but never takes a lock that a task holding the registry lock waits for.
- `added` / `removed` run under the registry lock: they make and end a connector and a mirror, and never wait.
- A write's copy is made old (`sys_watch_stale`) when its answer comes, never before: the refresh read must be after the write.
- The mirror's listener runs on the connector task: short, never blocks; the event goes on by `sys_event_publish_deferred`, to the event task.
