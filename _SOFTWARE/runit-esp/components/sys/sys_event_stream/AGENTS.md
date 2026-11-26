# sys_event_stream: working notes

- Read `SYS_EVENT_STREAM.MD` first. The module only connects `sys_event` and the data matrix through its port `EVENTS`; where the events go is links, made by the board (`app/runit`).
- `sys_event` must not know a port: keep this a component of its own.
- An event in is never published here: its source and channel are the sender's numbers, and no listener can tell them from this board's (another board's events reach the listeners only through `sys_device_remote`, as an alias's). It is passed on with `send_except(from)` only: `EVENTS` is never linked with another module port that passes on.
- The port's name (`MODULE_ID_SYS_EVENT`) and the packet `[hdr PUSH][sys_event_t][payload]` are wire format; the app reads them.
