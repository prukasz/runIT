# sys_event_stream

Component `components/sys/sys_event_stream/`. The events (SYS_EVENT.MD) as a stream of the data matrix (DATA_MATRIX.MD), **both ways**. It is a component of its own because `sys_session` needs `sys_event` and the matrix needs `sys_session`: `sys_event` can't know the matrix.

| Stream | Id | Direction | What |
|---|---|---|---|
| `EVENT_STREAM_NOTIFY` | `0x06`, urgent | out | every event with `SYS_EVENT_NOTIFY` in its flags (set in the events table: the ones that matter to the user), as the `sys_event_t` it is |
| `EVENT_STREAM_REMOTE` | `0x07` | in | a `sys_event_t` from another board: published here (`sys_event_publish`, deferred) without `SYS_EVENT_NOTIFY`, so two boards that route both ways do not pass it between them for ever. A frame whose length is not `sizeof(sys_event_t) + len` is dropped. |

`sys_event_stream_init()` (boot, after `sys_event_init` and `data_matrix_init`): gives the matrix the streams' flags, subscribes the listener of the notify events (`SYS_EVENT_RUN_FIRST`, the filter `SYS_EVENT_ANY_NOTIFY`: an action that stops the board can't hide its cause) and makes the sink, a callback end routed from `S(EVENT_STREAM_REMOTE)`.

Where they go and come from are the matrix's routes, none made here: `S(NOTIFY) → T(app's session, 0x06)` is the app's events (`app/runit`), and a slave's events reach this board with `T(uart to the slave, 0x06) → S(REMOTE)`; the same route may also go `→ T(app's session, 0x06)` to forward them.

An event that comes in has the **sender's** source and channel (its device ids): a board that has devices of another board as its own remote devices gets them mapped by `sys_remote` (`SYS_REMOTE.MD`), not by this stream.

## Tests

`host/test_sys_event_stream.c`: `python test_host/run.py sys_event_stream` (from `runit-esp/`): a notify event out, one without NOTIFY not out, an event in published without NOTIFY, a torn frame dropped.
