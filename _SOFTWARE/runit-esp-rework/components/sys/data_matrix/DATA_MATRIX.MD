# data_matrix

Component `components/sys/data_matrix/`. **The switch of the stream ports**: the output of one port goes to the inputs of the ports linked from it, unchanged. It knows nothing of what it carries: no role, no session, no tag, no format, no queue, no task. Root key `MODULE_ID_DATA_MATRIX` (`0x30`).

## Files

| File | Role |
|---|---|
| `data_stream.h` / `data_stream.c` | The switch: `data_stream_register` / `_unregister`, `data_stream_connect` / `_disconnect`, `data_stream_links`; the module's error tags registered |
| `router/data_stream_router.h` / `.c`, `router/contract_data_matrix.json` | The commands: `CONNECT`, `DISCONNECT`, `LIST` |
| `errors/` | `ERR_DATA_MATRIX_MAP` (`MODULE_ID_DATA_MATRIX` 0x30), its messages |
| `data_matrix_id.h` | `MODULE_ID_DATA_MATRIX` |

## Ports

A **port** (`core/utils/data_stream_port.h`, below every layer) is anything that sends or takes bytes that are not commands: a module's stream (`ERRORS`, `ERRORS_IN`, `LOGS`, ...), a provider (SYS_DATA_PROVIDER.MD "Stream port"). Its owner **embeds** a `data_stream_port_t` in its own struct and fills:

- its name, a `u16`: `DATA_STREAM_PORT(kind, id)`, the kind (`DATA_STREAM_PORT_MODULE` 0 or `DATA_STREAM_PORT_PROVIDER` 1) in the high byte;
- `pass`, its input (NULL: it takes nothing); in it the owner gets its struct back from `self` (`DATA_STREAM_OWNER`).

The switch fills `out` and `place` when it registers the port and clears them when it unregisters it. The owner sends with `data_stream_send(&port, data, len)`, an inline call of `out`: a module never links to the switch.

**The contract of a port:** `pass` is short, never blocks, never sends on its own port, copies what it keeps (`data` is valid for the call only); it runs on the sender's task. A send only from a task, never from an ISR, and no more once the port is unregistered. A packet with no link is dropped without a word. The order in which the targets of one output get a packet is no promise.

**Providers are ports by themselves:** the app hands the switch's `data_stream_register` / `data_stream_unregister` to the providers' registry (`sys_data_provider_set_port_hooks`, `app/runit`), so every provider made is registered (and every one already made) and every one removed is unregistered, its links with it. The switch and the providers never name each other.

## Links

A **link** `{from, to}` (`data_stream_link_t`, 4 bytes: two names, `u16` LE) sends the output of `from` to the input of `to`. `data_stream_connect` refuses an unknown port, a port to itself and `to` with no input (`ERR_DATA_MATRIX_BAD_ARG`), and a target past `CONFIG_DATA_STREAM_TARGETS` of one output (`ERR_DATA_MATRIX_FULL`); the same link again changes nothing. `data_stream_disconnect`: not there, nothing changes. `data_stream_unregister` takes the port's links with it.

**Synchronous:** a send calls the inputs of the linked ports at once, on the sender's task, with the packet unchanged. Since no `pass` sends on its own port, a send never runs inside a send: links both ways are safe and the switch checks no cycle.

Each port keeps its own targets, as 1-byte places in the table, so a send copies a few bytes under the spinlock and calls them after it: a `pass` never runs under the lock. The table changes from one task (boot, the commands).

## Commands (`router/data_stream_router.h`, `contract_data_matrix.json`)

| Op | Code | Args | Meaning |
|---|---|---|---|
| `CONNECT` (W) | `0x01` | `data_stream_link_t` | links the output of `from` to the input of `to` |
| `DISCONNECT` (W) | `0x02` | `data_stream_link_t` | removes that link |
| `LIST` (R) | `0x03` | | `u8` links, then a `data_stream_link_t` each, port by port; `ERR_CMD_OUT_FULL` when they do not fit the frame |

They are frames of the board's record (`SYS_ACTIONS_ID_DATA_MATRIX`), replayed at every start: the board's record makes the providers before it links them.

## Config (`Kconfig`)

`DATA_STREAM_PORTS` 32 (one place is 12 B on the board: 384 B), `DATA_STREAM_TARGETS` 4 (a send takes a pointer each of the sender's stack: 16 B).

## Open

- Not in the build: the ESP build and the host tests wait for the modules that used the matrix's streams, roles and callbacks to become ports: `sys_session` (its channels, a send to one session), `sys_log` (`LOGS`, the loop guard of a log line made in a delivery), the errors' stream (`ERRORS`, `ERRORS_IN`), `sys_event_stream`, `sys_watch`, `sys_remote`, `app/runit` (links instead of roles and routes).
- A self-registration macro for a module's port.
- A send in flight may still call the `pass` of a port unregistered meanwhile (its targets are copied under the lock, called after it): a provider removed in that moment (`REMOVE` frees its place) gets a stale call. Closing it needs a count of the sends in flight per port, which `unregister` waits for.
- Host tests of the switch.
