# sys_data_provider — providers

Component `components/sys/sys_data_provider/`. A **provider** is one way bytes go out of the board and come in: a BLE characteristic, a UART port. Code knows a provider by its **id**: **one owner holds its rx hook**, and **whoever knows the id may send**. The transport is its **driver's** (`sys_ble`, `sys_uart`).

## Files

| File | Role |
|---|---|
| `sys_data_provider.h` | Use: the id, open, close, send, max frame, state; the command binding |
| `sys_data_provider.c` | The registry: slots, one spinlock, lookup by id; what the router asks |
| `sys_data_provider_cmd.c` | The command binding: a provider's messages to the router, its answers back (Command binding) |
| `config/sys_data_provider_config.h` | What a provider's id and state are: the links, the state flags (published to the app) |
| `driver/sys_data_provider_driver.h` | For its drivers only: the provider (`sys_data_provider_t`), add, remove, mark system, set max frame, deliver |
| `internal/sys_data_provider_internal.h` | For its router only: LIST |
| `router/sys_data_provider_router.h` / `.c` | The remote interface (root key `ROUTER_KEY_PROVIDER` 0x0D). The header publishes every op (`SYS_DATA_PROVIDER_OP_*`, with its sides) and the wire layouts, each op above its frame |
| `router/contract_provider.json` | The router's descriptor |
| `errors/` | `ERR_SYS_DATA_PROVIDER_MAP`, its registration, the messages |
| `Kconfig` | Provider slots |

## The id

One byte, `uint8_t`: a provider's name in every API, record, error record and frame, and only a name. **No number means anything**, 0 too: whoever makes a provider chooses its id (UART CREATE, BLE CHAR_CREATE), so a stored project names it before it exists and gets the same one at every start, and no module compares it with a constant. A taken id: `ERR_PROV_EXISTS`. The board's own providers (the console, the link, the runIT pair, LOGS) get theirs from the board's configuration (`RUNIT_BOARD_PROVIDER_*`, `runit_board_cfg.h`), passed to their drivers' init, and carry `STATE_SYSTEM`. What the id stands for (a UART's port, a characteristic's UUID) is its driver's, and `link` (`sys_data_provider_link_e`) is a field of the provider that LIST shows.

A peer (the app, another board) is no provider: it is reached through a session (SYS_SESSION.MD), and a stream subscribes it by its id (SYS_STREAM.MD "Model").

Every call finds the provider by its id, so an id kept after the provider went answers `ERR_PROV_NOT_FOUND`, never a freed provider. A second provider with a taken id is refused at add (`ERR_PROV_EXISTS`).

## Use

- **The convention.** RX has one owner, whoever holds the hook. TX is free to whoever knows the id, and whoever binds or sends answers for what goes there: raw bytes on a session's line land between its frames, the peer reads them as a header and the session starts over (a binding stored in a project does so at every start). The board does not refuse it; the app does not offer a provider a session runs on.
- `sys_data_provider_open(id, rx, ctx)`: takes the rx hook (a session, an IN stream's binding, the command binding). The hook has an owner: `ERR_PROV_IN_USE`. An open provider can't be removed. The provider knows nothing of who its owner is: the context decides.
- `rx(ctx, id, data, len)` runs on the driver's RX task: it copies what it keeps and returns, never blocks, and sends only with `SYS_DATA_PROVIDER_SEND_NOWAIT`. `data` NULL: the provider went up or down, `len` 1 or 0; after open the user reads `sys_data_provider_is_up` once.
- `sys_data_provider_close(id)`: the provider is free again.
- `sys_data_provider_send(id, data, len, flags)`: one whole frame, by the id alone; no open needed. `SYS_DATA_PROVIDER_SEND_URGENT`: to the front of the driver's queue. Without `NOWAIT` it waits for room in that queue at most the driver's own bound (BLE, UART: their Kconfig), so never on a driver's RX task; with `NOWAIT` it only queues, and a full queue is `ERR_PROV_BUSY` at once: any task, a driver's RX task too. Too long: `ERR_PROV_TOO_LONG`, never cut. The driver's failure is wrapped in `ERR_PROV_SEND`.
- `sys_data_provider_is_up(id)`, `sys_data_provider_is_framed(id)`: bits of its state (Frame size). Read under the registry's spinlock, never a call into the driver: any task.
- `sys_data_provider_state(id)`: its state flags, a field as well. **`WIRED`** (`sys_data_provider_is_wired(id)`) says the link is a cable (UART): a round trip of about a millisecond, against tens of them, with a long tail, on a radio (BLE). `sys_session_wired` reads it.

## Frame size

A frame is whole above the driver: the driver splits it into what its link carries (BLE packets of MTU − 4 with the fragment header, a UART frame: UART below) and joins it on the way in. No one above it cuts a frame, and no size of a link is known above its driver: the one limit is `CMD_FRAME_MAX`, longer is refused (`ERR_PROV_TOO_LONG`). A provider is up or down (`SYS_DATA_PROVIDER_STATE_UP`), and framed or raw (`SYS_DATA_PROVIDER_STATE_FRAMED`).

| Provider | What it carries |
|---|---|
| framed (the runIT BLE characteristics, UART) | a whole `CMD_FRAME_MAX` frame (CMD.MD "Frame size"): the driver's buffers hold one, and the driver at the other end joins the pieces |
| raw (a user characteristic without the fragment header) | what its link takes at once, since the peer knows no fragments. A BLE one sends a longer message as several notifications, so the peer sees the bytes but not where the message ended |

A session takes a framed provider only (`sys_session_attach`: `ERR_SESSION_NOT_FRAMED`), so every frame between the board and its peers has the same limit. A raw one is for a stream's binding or a command binding. The board's providers today are the runIT BLE pair and LOGS (framed; SYS_BLE.MD "Providers"), the UART console and link (framed; SYS_UART.MD), the ports of UART CREATE (framed or raw) and the unframed providers of run-time BLE characteristics (SYS_BLE.MD "Run-time characteristics as providers"); the raw UART ports and the BLE ones are the only providers that take data from a peer other than a session.

## Driver

The driver makes a provider by its own command, from its own config (a BLE characteristic: ROUTER_KEY_BLE; a UART port: ROUTER_KEY_UART), fills the struct (`send`, `destroy`, `driver`, `id`, `link`, and in `state` `STATE_SYSTEM` for the board's own, `STATE_WIRED` when its link is a cable, `STATE_FRAMED` when it carries a whole frame, `STATE_NO_RX` when it delivers to no hook (a DIRECT UART port: its owner reads from its driver; `open` with an rx is `ERR_PROV_NO_RX`), `STATE_UP` when it is up already) and adds it (`sys_data_provider_add`). It hands every whole frame to `sys_data_provider_deliver` (it joins the fragments or the line first) and keeps up or down current with `sys_data_provider_set_up`, which tells the user when it changed.

`send` takes the flags (`URGENT`: the front of its queue, where the driver has one: UART; BLE keeps the order; `NOWAIT`: no wait for room) and raises `ERR_PROV_BUSY` itself when its queue is full. With `NOWAIT` it only puts the frame in its queue, which its own task empties: so it may be called from any driver's RX task, its own too, and never takes a lock that task holds. The rest of the struct (`rx`, `ctx`, `busy`, and `state`'s `OPEN` and `SYSTEM`) is the registry's, only touched under its spinlock; `busy` counts the sends in progress. Nothing runs under the spinlock but a lookup and a copy: the driver's `send`, its `destroy` and the user's `rx` are called after it is released. `state` is the one record of the provider's state: LIST sends it as is.

`sys_data_provider_remove(id)` is the only way a provider goes: refused while it is open or a send is in progress (`ERR_PROV_IN_USE`), or when it is the board's own (`ERR_PROV_SYSTEM`); else it is taken out, so its id finds nothing, and its driver's `destroy` frees it. The board's own providers (the runIT pair, the LOGS characteristic, the UART console and link) are made with `STATE_SYSTEM` set by their driver; a project's ERASE leaves them.

## UART

A framed UART provider (the console's, the link's, a port made framed) is **binary** (SYS_UART.MD); a raw one carries the bytes as they are:

- **A frame on the line**: COBS-encoded `[frame][crc16]`, between 0x00 bytes (COBS leaves no other 0x00). The receiver takes the bytes up to each 0x00 as one candidate: decoded, its CRC16-CCITT checked, then delivered whole. A corrupted byte fails the CRC and the frame is dropped (a request's source times out), so a broken byte never runs a command. COBS adds at most one byte in 254.
- **The console's port**: the ROM bootloader's lines and a panic's text are no frames: they fail the CRC and are dropped by the board. The host's tool shows the bytes that are no frame as text, so the boot and a panic stay readable. After the start the log lines go only as frames, through the LOGS stream (`sys_log`'s hook of `esp_log`), never as text (unless `mirror_serial`, SYS_LOG.MD).

## Command binding

A third owner of the rx hook, for a client outside runIT (a script, Node-RED, Home Assistant) that sends a command and waits for its answer with no session: no HELLO, no frame header. `sys_data_provider_bind_commands(id)` (or BIND_COMMANDS) opens the provider for it; `sys_data_provider_unbind_commands(id)` (UNBIND_COMMANDS) closes it.

```
 provider ──► command binding ──► router_submit (cmd_req_t: src ROUTER_SRC_NONE) ──► module
    ▲                                                                                 │
    └──────────── [status][data or the error record] ◄──── done (ctx: the binding) ◄──┘
```

- **In**: one message is one command, as the router takes it: `[key][dir][code | id + op][args]`, at most `CMD_DATA_MAX`. Its `seq` is the binding's own count of commands, its flags 0.
- **Out**: `[status][data]` on the same provider, `NOWAIT`: `status` `router_status_e`, `data` the answer or, on ERROR, the error record (ERRORS.MD "Error record"), as every answer has. The status goes into the byte in front of the router's answer, so there is no copy. A command the router refuses (too long, its queue full) is answered from the RX task and reported with its command, as the session does.
- **This board only**: nothing is forwarded; the answer goes back by the provider, not by an id. A provider whose rx hook has an owner: `ERR_PROV_IN_USE`.
- **No peer**: the command's `src` is `ROUTER_SRC_NONE` (0xFD, `SYS_SESSION_ID_NONE`). What the board keeps for a peer refuses it: WATCH ADD, STREAM SUBSCRIBE and REMOTE SUBSCRIBE (no one to send to), SYS DEADMAN set (`ERR_CMD_BAD_ARG`: no session whose silence it could notice). A client that follows a value reads it again, or takes a stream on a stream's binding.
- The router does not change: it knows `cmd_req_t` and `done`, not providers.

## Remote (`sys_data_provider_router`, root key `ROUTER_KEY_PROVIDER` 0x0D)

| Code | Command | Side | Request / answer |
|---|---|---|---|
| `0x01` | REMOVE | write | `u8 provider` |
| `0x02` | LIST | read | → `u8 count`, then `sys_data_provider_info_t` per provider (3 B: `u8 provider`, `u8 link`, `u8 state`: `SYS_DATA_PROVIDER_STATE_*`) |
| `0x03` | SEND | write | `u8 provider`, then the frame (a command and its answer on a raw UART: SYS_UART.MD "A command and its answer") |
| `0x04` | BIND_COMMANDS | write | `u8 provider`: its messages are commands for this board (Command binding) |
| `0x05` | UNBIND_COMMANDS | write | `u8 provider` |

- The id is chosen at the create (The id), so a project's records name it in the commands after the create.
- SEND is send as a command, so the app or another board sends through a provider of this one, also one an IN stream reads from (a request and its answer on a raw port). The convention above holds for it too.

## Limits

| Option | Default | Meaning |
|---|---|---|
| `CONFIG_SYS_DATA_PROVIDER_MAX` | 8 | provider slots, the board's own included |

Owners of the rx hook: the session (SYS_SESSION.MD, `sys_session_attach`), an IN stream's binding (SYS_STREAM.MD "Model") and the command binding (above). An OUT stream's binding holds nothing.
