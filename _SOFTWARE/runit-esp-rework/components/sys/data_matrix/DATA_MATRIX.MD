# data_matrix

Component `components/sys/data_matrix/`. **The switch of the stream ports**: the output of one port goes to the inputs of the ports linked from it, unchanged. It knows nothing of what it carries: no role, no session, no tag, no format, no queue, no task. Root key `MODULE_ID_DATA_MATRIX` (`0x30`).

**A minimal, synchronous implementation, by design.** The switch is a table of ports and their links and one call that hands a packet on, on the sender's task. It never waits, never queues, keeps no state of a port's owner and does no lifetime tracking: no reference counts, no waiting for sends in flight at an unregister. Everything else is the owner's: what a port does with a packet, its locking, and keeping the port's memory valid (the contract of a port, below).

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

The switch fills `out` and `place` when it registers the port and clears them when it unregisters it. The owner sends with `data_stream_send(&port, flags, data, len)`, an inline call of `out`: a module never links to the switch.

**The contract of a port:** `pass` is short, never blocks, never sends on its own port, copies what it keeps (`data` is valid for the call only); it runs on the sender's task. A send only from a task, never from an ISR, and no more once the port is unregistered. **The port's memory and its `pass` stay valid after it is unregistered:** a send in flight (its targets copied under the lock before the unregister, called after it) may still call it, so an owner never frees a port it has registered; its `pass` drops what comes after its owner is done (a provider's slot is never freed, SYS_DATA_PROVIDER.MD "Stream port"). A packet with no link is dropped without a word. The order in which the targets of one output get a packet is no promise.

**Providers are ports by themselves:** a provider's CREATE registers its port, its REMOVE unregisters it first (SYS_DATA_PROVIDER.MD "Stream port"). The switch never names a provider; the providers call the switch.

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
- Host tests of the switch.
