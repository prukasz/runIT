# sys_event_stream: working notes

- Read `SYS_EVENT_STREAM.MD` first. The module only connects `sys_event` and the data matrix; where the streams go is routes, made by the board (`app/runit`).
- `sys_event` must not depend on the matrix (a cycle through `sys_session`): keep this a component of its own.
- An event in is published without `SYS_EVENT_NOTIFY`: never pass the flag on, or two boards routed both ways echo it for ever.
- The stream ids (`0x06`, `0x07`) and the `sys_event_t` frame are wire format; the app reads `0x06`.
- Tests: `python test_host/run.py sys_event_stream` (from `runit-esp/`).
