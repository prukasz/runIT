# sys_event_stream

Component `components/sys/sys_event_stream/`. The events (SYS_EVENT.MD) as one port of the data matrix (DATA_MATRIX.MD), **both ways**: `EVENTS`, `DATA_MATRIX_PORT(MODULE, MODULE_ID_SYS_EVENT)`, each packet `[hdr PUSH, MODULE_ID_SYS_EVENT][sys_event_t][payload]`. A component of its own: `sys_event` knows no port.

| Way | What |
|---|---|
| out | every event with `SYS_EVENT_NOTIFY` in its flags (set in the events table: the ones that matter to the user), as the `sys_event_t` it is, urgent, `NOWAIT` |
| in | a `sys_event_t` from another board: passed on to every port linked from `EVENTS` but the one it came from (`data_matrix_send_except`), and published here (`sys_event_publish`, deferred) without `SYS_EVENT_NOTIFY`, so it does not go out again as this board's. A packet whose length is not `header + sizeof(sys_event_t) + len` is dropped. |

A constructor (before `app_main`; the component links `WHOLE_ARCHIVE`, as nothing outside calls it) registers `EVENTS` and subscribes the listener of the notify events (`SYS_EVENT_RUN_FIRST`, the filter `SYS_EVENT_ANY_NOTIFY`: an action that stops the board can't hide its cause).

Where they go and come from is the links (`app/runit`; those of a board's session, one attached as a slave or a master, are made by the attach: SYS_SESSION.MD): `EVENTS → S(app's session, MODULE_ID_SYS_EVENT)` is the app's events, and `S(a slave's session, MODULE_ID_SYS_EVENT) → EVENTS` takes a slave's, which `EVENTS` passes on to the app's session.

An event that comes in has the **sender's** source and channel (its device ids), and the board that made it is the packet's `src`: a board that has devices of another board as its own remote devices gets them mapped by `sys_device_remote` (`SYS_DEVICE_REMOTE.MD`), not by this port.
