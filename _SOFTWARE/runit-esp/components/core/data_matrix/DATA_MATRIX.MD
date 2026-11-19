# data_matrix

Component `components/core/data_matrix/` (in `core`: it depends on `core` only, and a module of any layer can have a port). **The switch of the ports**: the output of one port goes to the inputs of the ports linked from it, unchanged: to all of them, to one, or to all but one. It knows nothing of what it carries: no role, no session, no tag, no format, no queue, no task. Root key `MODULE_ID_DATA_MATRIX` (`0x30`).

**A minimal, synchronous implementation, by design.** The switch is a table of ports and their links and the calls that hand a packet on, on the sender's task. It never waits, never queues, keeps no state of a port's owner and does no lifetime tracking: no reference counts, no waiting for sends in flight at an unregister. Everything else is the owner's: what a port does with a packet, its locking, and keeping the port's memory valid (the contract of a port, below).

## Files

| File | Role |
|---|---|
| `data_matrix.h` / `data_matrix.c` | The port (`data_matrix_port_t`, its contract) and the switch: `data_matrix_send`, `_send_to`, `_send_except`, `data_matrix_register` / `_unregister`, `data_matrix_connect` / `_disconnect`, `data_matrix_links`; the module's error tags registered |
| `router/data_matrix_router.h` / `.c`, `router/contract_data_matrix.json` | The commands: `CONNECT`, `DISCONNECT`, `LIST` |
| `errors/` | `ERR_DATA_MATRIX_MAP` (`MODULE_ID_DATA_MATRIX` 0x30), its messages |
| `data_matrix_id.h` | `MODULE_ID_DATA_MATRIX` |

## Ports

A **port** (`data_matrix.h`) is anything a provider's bytes reach, the commands included: a module's (the router, a stream), a provider's (SYS_DATA_PROVIDER.MD "Stream port"), a session's. Its owner **embeds** a `data_matrix_port_t` in its own struct and fills:

- its name, a `u16`: `DATA_MATRIX_PORT(kind, id)`, the kind in the high byte: `DATA_MATRIX_PORT_MODULE` 0 (the id a module's number), `DATA_MATRIX_PORT_PROVIDER` 1 (a provider's id), `DATA_MATRIX_PORT_SESSION | provider` (`0x80 |` a provider's id below `0x80`: a session's ports, the id a module's number or 0 for the session's own);
- `in`, its input (NULL: it takes nothing); in it the owner gets its struct back from `self` (`DATA_MATRIX_OWNER`).

The switch keeps the port's `place` while it is registered. The owner sends out of its port, and a port that is not registered sends nothing:

| Call | To |
|---|---|
| `data_matrix_send(&port, flags, data, len)` | every port linked from it |
| `data_matrix_send_to(&port, to, flags, data, len)` | the one port `to`, when it is linked from it (`false`: no such link): an answer to `from`, a call to one session |
| `data_matrix_send_except(&port, except, flags, data, len)` | every port linked from it but `except`: what a port passes on, never back to the `from` it came from |

A link is the permission: no call reaches a port that is not linked from the sender. A taker gets the sender's name (`from`), its address to answer.

**The contract of a port:** `in` is short, never blocks, copies what it keeps (`data` is valid for the call only); it runs on the sender's task. A send only from a task, never from an ISR, and no more once the port is unregistered. **The port's memory and its `in` stay valid after it is unregistered:** a send in flight (its targets copied under the lock before the unregister, called after it) may still call it, so an owner never frees a port it has registered; its `in` drops what comes after its owner is done (a provider's slot is never freed, SYS_DATA_PROVIDER.MD "Stream port"). A packet with no link is dropped without a word. The order in which the targets of one output get a packet is no promise.

**Providers are ports by themselves:** a provider's CREATE registers its port, its REMOVE unregisters it first (SYS_DATA_PROVIDER.MD "Stream port"). The switch never names a provider; the providers call the switch.

## Links

A **link** `{from, to}` (`data_matrix_link_t`, 4 bytes: two names, `u16` LE) sends the output of `from` to the input of `to`. `data_matrix_connect` refuses an unknown port, a port to itself and `to` with no input (`ERR_DATA_MATRIX_BAD_ARG`), and a target past `CONFIG_DATA_MATRIX_TARGETS` of one output (`ERR_DATA_MATRIX_FULL`); the same link again changes nothing. `data_matrix_disconnect`: not there, nothing changes. `data_matrix_unregister` takes the port's links with it.

**Synchronous:** a send calls the inputs of the linked ports at once, on the sender's task, with the packet unchanged.

**A send inside an `in`** ends at a provider: a provider's `in` never sends. A session's `in` sends on at once (what its provider brings out of a sub-port, what comes into a sub-port out to its provider); a module's `in` may pass on what it takes (`data_matrix_send_except(from)`) or send from a callback (the router's call). The modules' ports that send in their `in` are linked with sessions' sub-ports and providers only, never with each other: no chain is a cycle, and the switch checks none. Each session on a chain puts its frame on the sender's stack.

Each port keeps its own targets, as 1-byte places in the table, so a send copies a few bytes under the spinlock and calls them after it: an `in` never runs under the lock. The table changes from one task (boot, the commands).

## Commands (`router/data_matrix_router.h`, `contract_data_matrix.json`)

| Op | Code | Args | Meaning |
|---|---|---|---|
| `CONNECT` (W) | `0x01` | `data_matrix_link_t` | links the output of `from` to the input of `to` |
| `DISCONNECT` (W) | `0x02` | `data_matrix_link_t` | removes that link |
| `LIST` (R) | `0x03` | | `u8` links, then a `data_matrix_link_t` each, port by port; `ERR_CMD_OUT_FULL` when they do not fit the frame |

They are frames of the board's record (`SYS_ACTIONS_ID_DATA_MATRIX`), replayed at every start: the board's record makes the providers before it links them.

## Config (`Kconfig`)

`DATA_MATRIX_PORTS` 32 (one place is 16 B on the board: 512 B), `DATA_MATRIX_TARGETS` 8 (the router's and the streams' ports are linked with every session; a send takes a pointer each of the sender's stack: 32 B).

## Open

- Not in the build: the ESP build and the host tests wait for the modules that used the matrix's streams, roles and callbacks to become ports, by the design below: `sys_session`, `router_queue`, `sys_log` (`LOGS`, the loop guard of a log line made in a delivery), the errors' stream, `sys_event_stream`, `sys_watch`, `sys_remote`, `app/runit` (links instead of roles and routes).
- A self-registration macro for a module's port.
- Host tests of the switch.

### The design to build: every byte of a provider through the matrix

Everything a provider carries goes through the matrix, the commands too, and what is connected is connected by links only. Nothing in it routes: a port that answers or calls names its target, and the link is the permission.

**One header on every frame and every packet**, 5 B: `[type u8][src u8][key u8][seq u16]`. A packet in the matrix is the frame of the line as it is: a session translates nothing.

| Frame | `type` | `src` | `key` | `seq` | After the header |
|---|---|---|---|---|---|
| `REQUEST` | the kind, `URGENT`, `NOREPLY` | who gave the command | the router's key (a module) | the sender's counter | `[dir][code \| id + op][args]` |
| `ANSWER` | the kind, the status, `URGENT` | who answers | the request's | the request's | the data, or the error record |
| `PUSH` | the kind, `URGENT` | the board that made it | the module (its stream) | its class's count on this link | the data |
| `CONTROL` | the kind | the sender | `MODULE_ID_SESSION` | 0 | `[op][data]` |

- **`src` 0 is none.** Board ids are `0x01..0xEF` (a board with no id given has 0 and its frames go without one), clients `0xF0..0xFC` (`CMD_SRC_*`); the hello has no `id` (the sender is `src`; it keeps `boot_id`, `build_id`). A hello with `src` 0 still opens a session, so the app can give a new board its id.
- **Who sets `src`:** the session that puts a frame on the line, its board's id, and only when `src` is 0. Nothing else touches it, so a slave's error keeps the slave's id through every board on its way to the app.
- `src` and `key` go end to end; `seq` is per link: a session gives a PUSH its class's count, the router a call the `seq` of its slot.
- `CMD_HEAD` is 5: `key` moves from the body into the header, `src` is the one new byte.
- Done in `core/cmd` (CMD.MD "The header": `cmd_hdr_t`, `CMD_TYPE_*`, `cmd_kind_e`, `CMD_SRC_*`) and `MODULE_ID_ERRORS` (`core/errors/errors_id.h`); the router and the session do not write it yet.

**The ports.** A module's (the router, `ERRORS`, the events, `LOGS`, the watches, the remote devices), a provider's, a session's. A module has at most one stream port, named by its module number: the port's name, the PUSH's `key` and the router's key are the same number. `ERRORS` is `core/errors`'s, `MODULE_ID_ERRORS` `0x31` (the errors' stream leaves `sys_log`, whose port is the logs, `0x2A`); the events `0x25`, the watches `0x2B`, the remote devices `0x2F`.

**A session** is a port linked both ways with its provider, and the sub-ports it owns: the command sub-port (`MODULE_ID_ROUTER`), and one per stream module it shows. The session port and its sub-ports are joined inside the session (no link, not in `LIST`); the sub-ports are linked in the matrix with the modules' ports. Their names: the high byte `0x80 | provider` (provider ids are below `0x80`), the low byte 0 for the session port, else the module's number. `ATTACH {provider, deadman_ms, flags, n, modules[]}`, a command of `sys_session` (replacing the matrix's role), registers them and links the session port with its provider both ways; `DETACH` unregisters them (their links go). What a session does with a frame:

| Frame | Goes |
|---|---|
| `CONTROL` | the session's own (hello, ping, NO_SESSION, the dead-man) |
| `REQUEST`, `ANSWER` | out of its command sub-port, to the router; from it back onto the line |
| `PUSH` | out of the sub-port of its `key` (none: dropped); what comes into a sub-port by a link leaves as a PUSH with its class's `seq`, `NOWAIT \| SPARE` |

The channel range (`0x80`, `SYS_SESSION_CHANNEL`) is gone: the watches and the remote devices are streams like the errors. A module learns the session a packet came on from `from`, the board from `src`.

**The router has one port**, `ROUTER` (`MODULE_ID_ROUTER`), for the sessions' command sub-ports and for providers without a session (an MQTT topic of commands) alike: a raw provider carries the header like any bytes, the client on its other end writes it (`src` 0 and `seq` 0 when it does not need them). Commands through a raw provider are full access on purpose: no dead-man and no safety flag stand behind them.

`n` connections are `n` links into the one port, not `n` ports: an answer goes back with `data_matrix_send_to(port, from, ...)` to whoever asked. The router is the hub of commands both ways: this board's calls to a peer leave from `ROUTER` too. The caller (`sys_remote`, the watches' client) finds the command sub-port of `dst`'s session (`sys_session_route`) and gives it to the router, which keeps the call's slot by `(from, seq)`, its timeout and its wait (`router_request`, ROUTER_QUEUE.MD "Calls": done). A broadcast (STOP, the time) is a plain `data_matrix_send` from `ROUTER` with `NOREPLY`: to every session linked. C code of this board runs its commands as now (`router_submit`, `router_inject`).

**Streams: one port per stream, both ways.** `ERRORS` sends this board's records and takes the records of other boards; the events the same (`ERRORS` / `ERRORS_IN` and `EVENT_NOTIFY` / `EVENT_REMOTE` become one port each). What a stream port takes it passes on with `data_matrix_send_except(port, from, ...)`: never back to the board it came from, so a slave's errors reach the app's session and never return to the slave. Three boards linked in a ring are not guarded: boards make a tree. What a module does on its own board with what it takes (the log's history, the policy, publishing an event) is decided when the module moves to ports, not here.

**Raw providers** link with the modules' ports directly, with no session: `ERRORS` to a raw BLE characteristic or an MQTT topic, `ROUTER` ⇄ an MQTT topic of commands. A raw provider of commands carries whole messages (MQTT, a BLE characteristic): on a byte stream with no framing the header has nowhere to start.

**A tunnel** is two links, `P(a) ⇄ P(b)`: the frames pass whole, so the app opens a session with the board behind. A provider in a tunnel has no session (its output goes to every target), and both ends are framed or both raw.

**Still to settle in the work:**

- The deepest chain (provider, session, its sub-port, the router's call callback, another session, its provider) puts a session frame on the sender's stack at each session: counted against the BLE host's and the RX tasks' stacks.
- `DATA_MATRIX_PORTS` counted again: each session is its port, its command sub-port and one per stream module (a Kconfig limit of them per session).
- The session's end fails the router's calls through it (`router_request_fail_to(port)`, called by the session); `sys_data_provider_state` public (`FRAMED` at `ATTACH`, `WIRED` for `sys_session_wired`); the broadcast from `ROUTER`.

**The order of work:** `sys_session` (the session port, the sub-ports, `ATTACH` / `DETACH`, the providers' events instead of their open / send), the stream ports (`sys_log`, `sys_event_stream`), `sys_watch` and `sys_remote` on tags, `app/runit` (`ATTACH` and `CONNECT` frames instead of roles and routes), `contract_session.json`, `session_client.py` and the app's session code.
