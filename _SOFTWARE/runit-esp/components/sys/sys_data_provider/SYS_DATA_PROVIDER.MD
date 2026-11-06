# sys_data_provider — providers

Component `components/sys/sys_data_provider/`. A **provider** is one way bytes go out of the board and come in: a BLE characteristic, a UART port. Code knows a provider by its **id**: one **user** opens it, sends on it, gets what comes in and closes it. The transport is its **driver's** (`sys_ble`, `sys_uart`).

## Files

| File | Role |
|---|---|
| `sys_data_provider.h` | Use: the id, open, close, send, max frame, state; the command binding |
| `sys_data_provider.c` | The registry: slots, one spinlock, lookup by id; what the router asks |
| `sys_data_provider_cmd.c` | The command binding: a provider's messages to the router, its answers back (Command binding) |
| `config/sys_data_provider_config.h` | What a provider's id and state are: the links, the state flags (published to the app) |
| `driver/sys_data_provider_driver.h` | For its drivers only: the provider (`sys_data_provider_t`), add, remove, mark system, set max frame, deliver |
| `internal/sys_data_provider_internal.h` | For its router only: LIST, SEND to a provider no user has |
| `router/sys_data_provider_router.h` / `.c` | The remote interface (root key `ROUTER_KEY_PROVIDER` 0x0D). The header publishes every op (`SYS_DATA_PROVIDER_OP_*`, with its sides) and the wire layouts, each op above its frame |
| `router/contract_provider.json` | The router's descriptor |
| `errors/` | `ERR_SYS_DATA_PROVIDER_MAP`, its registration, the messages |
| `Kconfig` | Provider slots |

## The id

`sys_data_provider_id_t {u8 link, u32 channel}`, 5 bytes, packed: a provider's name in records, error records and the app, kept and sent as is. Above the drivers it is kept, passed and shown, never branched on; `sys_data_provider_same_id(a, b)` (inline, in the header) is the one comparison of two ids.

| Link | Channel |
|---|---|
| UART | the port |
| BLE | the characteristic's 16-bit UUID (of an RX / TX pair: TX) |

A peer (the app, another board) is no provider: it is reached through a session (SYS_SESSION.MD), and a stream subscribes it by its id (SYS_STREAM.MD "Model").

Every call finds the provider by its id, so an id kept after the provider went answers `ERR_PROV_NOT_FOUND`, never a freed provider. A second provider with a taken id is refused at add (`ERR_PROV_EXISTS`).

## Use

- `sys_data_provider_open(id, rx, ctx)`: a provider has **one user** at a time (a session, a stream's binding, the command binding): it alone sends on it and gets what comes in (`rx` NULL: send only), so bytes of two senders never mix on one line. Open by another: `ERR_PROV_IN_USE`. An open provider can't be removed. The provider knows nothing of who its user is: the context decides.
- `rx(ctx, id, data, len)` runs on the driver's RX task: it copies what it keeps and returns, never blocks, and sends only with `SYS_DATA_PROVIDER_SEND_NOWAIT`. `data` NULL: the provider went up or down, `len` 1 or 0; after open the user reads `sys_data_provider_is_up` once.
- `sys_data_provider_close(id)`: the provider is free again.
- `sys_data_provider_send(id, data, len, flags)`: one whole frame, on a provider the caller opened. `SYS_DATA_PROVIDER_SEND_URGENT`: to the front of the driver's queue. Without `NOWAIT` it waits for room in that queue at most the driver's own bound (BLE, UART: their Kconfig), so never on a driver's RX task; with `NOWAIT` it only queues, and a full queue is `ERR_PROV_BUSY` at once: any task, a driver's RX task too. Too long: `ERR_PROV_TOO_LONG`, never cut. The driver's failure is wrapped in `ERR_PROV_SEND`.
- `sys_data_provider_is_up(id)`, `sys_data_provider_is_framed(id)`: bits of its state (Frame size). Read under the registry's spinlock, never a call into the driver: any task.
- `sys_data_provider_state(id)`: its state flags, a field as well. **`WIRED`** (`sys_data_provider_is_wired(id)`) says the link is a cable (UART): a round trip of about a millisecond, against tens of them, with a long tail, on a radio (BLE). `sys_session_wired` reads it.

## Frame size

A frame is whole above the driver: the driver splits it into what its link carries (BLE packets of MTU − 4 with the fragment header, a UART frame: UART below) and joins it on the way in. No one above it cuts a frame, and no size of a link is known above its driver: the one limit is `CMD_FRAME_MAX`, longer is refused (`ERR_PROV_TOO_LONG`). A provider is up or down (`SYS_DATA_PROVIDER_STATE_UP`), and framed or raw (`SYS_DATA_PROVIDER_STATE_FRAMED`).

| Provider | What it carries |
|---|---|
| framed (the runIT BLE characteristics, UART) | a whole `CMD_FRAME_MAX` frame (CMD.MD "Frame size"): the driver's buffers hold one, and the driver at the other end joins the pieces |
| raw (a user characteristic without the fragment header) | what its link takes at once, since the peer knows no fragments; the driver refuses more (BLE: `ERR_BLE_TOO_LONG`, wrapped in `ERR_PROV_SEND`). Whoever sends on it knows that size |

A session takes a framed provider only (`sys_session_attach`: `ERR_SESSION_NOT_FRAMED`), so every frame between the board and its peers has the same limit. A raw one is for a stream's binding or a command binding. The board's providers today are the runIT BLE pair and LOGS (framed; SYS_BLE.MD "Providers"), the UART console and link (framed; SYS_UART.MD) and the unframed providers of run-time BLE characteristics (SYS_BLE.MD "Run-time characteristics as providers"), the only ones that take data from a peer other than a session.

## Driver

The driver makes a provider from its own config (a BLE characteristic, a UART port), fills the struct (`send`, `destroy`, `driver`, `id`, and in `state` `STATE_SYSTEM` for the board's own, `STATE_WIRED` when its link is a cable, `STATE_FRAMED` when it carries a whole frame, `STATE_UP` when it is up already) and adds it (`sys_data_provider_add`). It hands every whole frame to `sys_data_provider_deliver` (it joins the fragments or the line first) and keeps up or down current with `sys_data_provider_set_up`, which tells the user when it changed.

`send` takes the flags (`URGENT`: the front of its queue, where the driver has one: UART; BLE keeps the order; `NOWAIT`: no wait for room) and raises `ERR_PROV_BUSY` itself when its queue is full. With `NOWAIT` it only puts the frame in its queue, which its own task empties: so it may be called from any driver's RX task, its own too, and never takes a lock that task holds. The rest of the struct (`rx`, `ctx`, `busy`, and `state`'s `OPEN` and `SYSTEM`) is the registry's, only touched under its spinlock; `busy` counts the sends in progress. Nothing runs under the spinlock but a lookup and a copy: the driver's `send`, its `destroy` and the user's `rx` are called after it is released. `state` is the one record of the provider's state: LIST sends it as is.

`sys_data_provider_remove(id)` is the only way a provider goes: refused while it is open or a send is in progress (`ERR_PROV_IN_USE`), or when it is the board's own (`ERR_PROV_SYSTEM`); else it is taken out, so its id finds nothing, and its driver's `destroy` frees it. The board's own providers (the runIT pair, the LOGS characteristic, the UART console and link) are made with `STATE_SYSTEM` set by their driver; a project's ERASE leaves them.

## UART

A UART provider is always **binary**, the console's port too (SYS_UART.MD):

- **A frame on the line**: COBS-encoded `[frame][crc16]`, between 0x00 bytes (COBS leaves no other 0x00). The receiver takes the bytes up to each 0x00 as one candidate: decoded, its CRC16-CCITT checked, then delivered whole. A corrupted byte fails the CRC and the frame is dropped (a request's source times out), so a broken byte never runs a command. COBS adds at most one byte in 254.
- **The console's port**: the ROM bootloader's lines and a panic's text are no frames: they fail the CRC and are dropped by the board. The host's tool shows the bytes that are no frame as text, so the boot and a panic stay readable. After the start the log lines go only as frames, through the LOGS stream (`sys_log`'s hook of `esp_log`), never as text (unless `mirror_serial`, SYS_LOG.MD).

## Command binding

A provider's third user, for a client outside runIT (a script, Node-RED, Home Assistant) that sends a command and waits for its answer with no session: no HELLO, no frame header. `sys_data_provider_bind_commands(id)` (or BIND_COMMANDS) opens the provider for it; `sys_data_provider_unbind_commands(id)` (UNBIND_COMMANDS) closes it.

```
 provider ──► command binding ──► router_submit (cmd_req_t: src ROUTER_SRC_NONE) ──► module
    ▲                                                                                 │
    └──────────── [status][data or the error record] ◄──── done (ctx: the binding) ◄──┘
```

- **In**: one message is one command, as the router takes it: `[key][dir][code | id + op][args]`, at most `CMD_DATA_MAX`. Its `seq` is the binding's own count of commands, its flags 0.
- **Out**: `[status][data]` on the same provider, `NOWAIT`: `status` `router_status_e`, `data` the answer or, on ERROR, the error record (ERRORS.MD "Error record"), as every answer has. The status goes into the byte in front of the router's answer, so there is no copy. A command the router refuses (too long, its queue full) is answered from the RX task and reported with its command, as the session does.
- **This board only**: nothing is forwarded; the answer goes back by the provider, not by an id. A provider another user has: `ERR_PROV_IN_USE`.
- **No peer**: the command's `src` is `ROUTER_SRC_NONE` (0xFD, `SYS_SESSION_ID_NONE`). What the board keeps for a peer refuses it: WATCH ADD, STREAM SUBSCRIBE and REMOTE SUBSCRIBE (no one to send to), SYS DEADMAN set (`ERR_CMD_BAD_ARG`: no session whose silence it could notice). A client that follows a value reads it again, or takes a stream on a stream's binding.
- The router does not change: it knows `cmd_req_t` and `done`, not providers.

## Remote (`sys_data_provider_router`, root key `ROUTER_KEY_PROVIDER` 0x0D)

| Code | Command | Side | Request / answer |
|---|---|---|---|
| `0x01` | REMOVE | write | provider id |
| `0x02` | LIST | read | → `u8 count`, then `sys_data_provider_info_t` per provider (provider id, `u16` max frame now, `u8 state`: `STATE_SYSTEM`, `STATE_OPEN`, `STATE_WIRED`) |
| `0x03` | SEND | write | provider id, then the frame |
| `0x04` | BIND_COMMANDS | write | provider id: its messages are commands for this board (Command binding) |
| `0x05` | UNBIND_COMMANDS | write | provider id |

- The id is known before the driver answers (a UUID, a port), so a project's records name it in the commands after the create.
- SEND is send as a command, so the app or another board sends through a provider of this one. An open provider is its user's: SEND to it is refused (`ERR_PROV_IN_USE`).

## Limits

| Option | Default | Meaning |
|---|---|---|
| `CONFIG_SYS_DATA_PROVIDER_MAX` | 8 | provider slots, the board's own included |

Users: the session (SYS_SESSION.MD, `sys_session_attach`), a stream's binding (SYS_STREAM.MD "Model") and the command binding (above).

## Open

- **UART create command**: opening a UART port as a provider with a command (port, baud, pins, flow control), for a raw UART as a stream binding or a command binding. Today the board config opens its UART providers in C.
