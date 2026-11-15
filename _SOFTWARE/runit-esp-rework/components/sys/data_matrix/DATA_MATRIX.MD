# data_matrix

Component `components/sys/data_matrix/`. **Where the data that are not commands go**, set from one place: a **role** per provider (what it carries) and **routes** from one **end** to another. The logs to a BLE characteristic, a bridge between two providers, a watch's entries to the session that asked for them, a board's errors to the app are routes of one table, written in one format, listed by one command and replayed at boot. Root key `MODULE_ID_DATA_MATRIX` (`0x30`).

The module is a table and the delivery along it. It knows no protocol: the session's hello, calls and requests stay in `sys_session`, the commands in the router. It keeps no state of its own beyond the tables, and no counters.

## Roles

A provider has at most one role, given by `ROLE` and kept until `CLEAR_ROLE`. A provider with **no role is not attached**: what comes in on it is dropped, and no route may have it.

| Role | The provider |
|---|---|
| `RAW` | carries bytes as they are, no frame and no tag; its rx hook is the matrix's, what comes in goes along the routes from `P(provider)` |
| `SESSION` | has a session on it (`sys_session_attach`): REQUEST, ANSWER and CONTROL are the session's, a PUSH `[tag][data]` is data of the matrix (`T`). Parameters: the session's dead-man time and `SYS_SESSION_SAFETY` (`SYS_SESSION.MD`) |
| `COMMANDS` | is a command port: each message in is one command `[key][dir][code \| id + op][args]` for the router with `src` `ROUTER_SRC_NONE`, its answer goes back on the provider as `[status][data]`, NOWAIT |

`ROLE` again with the same arguments does nothing (a stored record runs at every start); with another role it is `ERR_DATA_MATRIX_ROLE`, `CLEAR_ROLE` first. `CLEAR_ROLE` removes the provider's routes with it, ends its session and gives its rx hook back. A provider that has a role cannot be removed (`ERR_PROV_IN_USE`).

## Ends and routes

| End | As a source | As a target |
|---|---|---|
| `P(provider)` | the raw bytes of a `RAW` provider | the same bytes sent |
| `T(provider, tag)` | the PUSH with that tag on a `SESSION` provider (`provider` may be `DATA_MATRIX_ANY`: that tag from every session) | a PUSH `[tag][data]` on a `SESSION` provider, the data as they are on a `RAW` one (the tag is not sent) |
| `S(stream)` | what the board's code sent with `data_matrix_send(stream, ...)` | the data enter the stream |
| `C(callback)` | never | `data_matrix_listen(fn, ctx, &end)`: the board's code, `fn(ctx, from, data, len)` |

A route `{from, to}` (6 bytes) is allowed for `from` P or T to any `to`, and for `from` S to P, T or C (a stream does not feed a stream). A provider's end needs its role when the route is made (`ERR_DATA_MATRIX_PAIR` otherwise). Several routes may leave one end and several may arrive at one: the data are copied to each, NOWAIT, never blocked on; what a target does not take (a busy or down provider, a session not open) is lost without a word, as the streams' always were. `from` in the callback is the end the data came out of, so a consumer that answers (a watch's cache: the board of the session) knows where they came from.

## Streams and the module's tags

A stream is an id (`1..DATA_MATRIX_STREAM_LAST`, `0x7F`) with no object behind it: `data_matrix_send(id, data, len)` queues the data (never blocks, any task, copied), the matrix's task delivers them to every route from `S(id)`. A stream with no route drops them. A module's channel on a session (the watch's entries, a remote device's frames) is a tag from `0x80`, `SYS_SESSION_CHANNEL(MODULE_ID_x)`, and uses the same ends: the module listens with `data_matrix_listen` and routes `T(DATA_MATRIX_ANY, channel)` to its callback, and answers a given session with `data_matrix_send_to(T(provider, channel), ...)`.

### Streams of a module

A module that sends on streams declares them in a table of its own, like its events and errors, in its header:

```c
#define LOG_STREAMS(X, P)                                       \
  X(P, LOGS,   0x03, 0,                  "the log lines, text")  \
  X(P, ERRORS, 0x04, DATA_MATRIX_URGENT, "the error records")
DATA_MATRIX_STREAMS_DECLARE(LOG)   // LOG_STREAM_LOGS = 0x03, LOG_STREAM_ERRORS = 0x04, LOG_STREAMS_COUNT
```

One `.c` has `DATA_MATRIX_STREAMS_DEFINE(LOG)` (the table `LOG_streams`, and a symbol per id, so two modules with one id do not link; the id is written with two hex digits), and the module's init gives the matrix its flags: `data_matrix_streams(LOG_streams, LOG_STREAMS_COUNT)`. `DATA_MATRIX_URGENT`: the stream's frames go to the front of every provider's queue. The ids in use: `LOGS` `0x03`, `ERRORS` `0x04` (`sys_log.h`), `EVENTS` `0x06` (`app/runit`).

## Commands (`router/data_matrix_router.h`, `contract_data_matrix.json`)

| Op | Args | Meaning |
|---|---|---|
| `ROLE` (W) | `data_matrix_role_args_t` {provider, role, dead-man u16, flags} | gives the role |
| `CLEAR_ROLE` (W) | {provider} | takes it away, with the provider's routes |
| `ROUTE` / `UNROUTE` (W) | `data_matrix_route_t` | adds / removes a route; the same again changes nothing |
| `CLEAR` (W) | `data_matrix_end_t` | removes every route that has the end, on either side |
| `LIST` (R) | | `u8` roles, each `data_matrix_role_args_t`, `u8` routes, each `data_matrix_route_t` |

They are frames of the board's record (`SYS_ACTIONS_ID_DATA_MATRIX`): `ROLE` first, then the `ROUTE`s, replayed at every start.

## The board's code

`data_matrix_init()` (boot, after `sys_session_init`) starts the task and takes the sessions' PUSH frames (`sys_session_set_push`). `data_matrix_send`, `data_matrix_send_to` (to one end with no route, any task), `data_matrix_listen`, `data_matrix_streams`, `data_matrix_is_task` (a log line made inside a delivery is not sent again). The callback runs on the task that delivers: the provider's rx task for `P` and `T`, the matrix's task for `S`; it copies what it keeps and never blocks.

## Config (`Kconfig`)

`DATA_MATRIX_ROLES` 8, `DATA_MATRIX_ROUTES` 24 (a delivery copies the matching routes' targets, 3 bytes each, onto its stack), `DATA_MATRIX_CALLBACKS` 6, `DATA_MATRIX_QUEUE_SIZE` 4608 (the stream sends and what a route puts in a stream; a full queue drops), `DATA_MATRIX_TASK_PRIO` 4 (below the router's: streams wait, commands don't), `DATA_MATRIX_TASK_STACK` 4096.

## Tests

`host/test_data_matrix.c`: `python test_host/run.py data_matrix` (from `runit-esp/`), with the real providers' registry, the router, the sessions and the matrix's task on fake providers: roles, the allowed pairs and the table's limits, a stream to every route (urgent first), a bridge, a provider into a stream and a callback, a tagged PUSH to its routes only and from any session, the tag on a session and bare on a raw provider, a command port (its answer on the provider) and the remote commands.
