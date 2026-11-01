# sys_data_provider — providers (draft)

Component `components/sys/sys_data_provider/` (draft in `_wire_draft/`, not built). A **provider** is one way bytes go out of the board and come in: a BLE characteristic, a UART port. Code knows a provider by its **id**: one **user** opens it, sends on it, gets what comes in and closes it. The transport is its **driver's** (`sys_ble`, `sys_uart`).

## Files

| File | Role |
|---|---|
| `sys_data_provider.h` | Use: the id, open, close, send, max frame, state |
| `config/sys_data_provider_config.h` | What a provider's id and state are: the links, the state flags (published to the app) |
| `driver/sys_data_provider_driver.h` | For its drivers only: the provider (`sys_data_provider_t`), add, remove, mark system, set max frame, deliver |
| `router/sys_data_provider_router.h` | The remote interface (root key `ROUTER_KEY_PROVIDER` 0x0D) |
| `errors/` | `ERR_SYS_DATA_PROVIDER_MAP`, its registration, the messages |

## The id

`sys_data_provider_id_t {u8 link, u32 channel}`, 5 bytes, packed: a provider's name in records, error records and the app, kept and sent as is. Above the drivers it is kept, passed and shown, never branched on.

| Link | Channel |
|---|---|
| UART | the port |
| BLE | the characteristic's 16-bit UUID (of an RX / TX pair: TX) |

A peer (the app, another board) is no provider: it is reached through a session (SYS_SESSION.MD), and a connector subscribes it by its id (SYS_DATA_CONNECTOR.MD "Subscribers").

Every call finds the provider by its id, so an id kept after the provider went answers `ERR_PROV_NOT_FOUND`, never a freed provider. A second provider with a taken id is refused at add (`ERR_PROV_EXISTS`).

## Use

- `sys_data_provider_open(id, rx, ctx)`: a provider has **one user** at a time (a session, a connector's binding): it alone sends on it and gets what comes in (`rx` NULL: send only), so bytes of two senders never mix on one line. Open by another: `ERR_PROV_IN_USE`. An open provider can't be removed. The provider knows nothing of who its user is: the context decides.
- `rx(ctx, id, data, len)` runs on the driver's RX task: it copies what it keeps and returns, never blocks, and sends only with `SYS_DATA_PROVIDER_SEND_NOWAIT` (the session forwards a frame from there). `data` NULL: the provider went up or down, `len` its `max_frame` now (0: down); after open the user reads `sys_data_provider_max_frame` once.
- `sys_data_provider_close(id)`: the provider is free again.
- `sys_data_provider_send(id, data, len, flags)`: one whole frame, on a provider the caller opened. `SYS_DATA_PROVIDER_SEND_URGENT`: to the front of the driver's queue. Without `NOWAIT` it waits for room in that queue at most the driver's own bound (BLE, UART: their Kconfig), so never on a driver's RX task; with `NOWAIT` it only queues, and a full queue is `ERR_PROV_BUSY` at once: any task, a driver's RX task too. Too long: `ERR_PROV_TOO_LONG`, never cut. The driver's failure is wrapped in `ERR_PROV_SEND`.
- `sys_data_provider_max_frame(id)`: the largest whole frame it delivers now (Frame size); 0 = down. A field, so reading it takes no lock and never calls the driver.
- `sys_data_provider_state(id)`: its state flags, a field as well. **`WIRED`** says the link is a cable (UART, SPI between two boards or two chips of one board): a round trip of about a millisecond, against tens of them, with a long tail, on a radio (BLE, MQTT, ESP-NOW). The session takes its call timeouts by it, and only a wired way lets a caller wait for an answer in the VM (SYS_SESSION.MD "Answers").

## Frame size

A frame is whole above the driver: the driver splits it into what its link carries (BLE packets of MTU − 4 with the fragment header, a UART frame: UART below) and joins it on the way in. No one above it cuts a frame: one longer than `max_frame` is refused (`ERR_PROV_TOO_LONG`).

| Provider | `max_frame` when up | Changes |
|---|---|---|
| framed (the runIT BLE characteristics, UART) | `CMD_FRAME_MAX` (CMD.MD "Frame size"): the driver's buffers hold one | up, down (0) |
| unframed (a user characteristic without the fragment header) | one packet: min(MTU − 3, `CMD_FRAME_MAX`), since the peer knows no fragments | also the MTU exchange |

A session takes only a framed provider, so every frame between boards and clients has the same limit; only a connector bound to an unframed one sizes its payloads by `max_frame` (`sys_data_connector_max_payload`).

## Driver

The driver makes a provider from its own config (a BLE characteristic, a UART port), fills the struct (`send`, `destroy`, `max_frame`, `driver`, `id`, and in `state` `STATE_FRAMED` when it splits and joins frames, `STATE_WIRED` when its link is a cable) and adds it (`sys_data_provider_add`). It hands every whole frame to `sys_data_provider_deliver` (it joins the fragments or the line first) and keeps `max_frame` current with `sys_data_provider_set_max_frame` (Frame size), which tells the user when it changed.

`send` takes the flags (`URGENT`: the front of its queue; `NOWAIT`: no wait for room) and raises `ERR_PROV_BUSY` itself when its queue is full. With `NOWAIT` it only puts the frame in its queue, which its own task empties: so it may be called from any driver's RX task, its own too, and never takes a lock that task holds. The rest of the struct (`rx`, `ctx`, `busy`, and `state`'s `OPEN` and `SYSTEM`) is the registry's, only touched under its lock; `busy` counts the sends in progress. `state` is the one record of the provider's state: LIST sends it as is.

`sys_data_provider_remove(id)` is the only way a provider goes, also when its driver removes what it stands on (a BLE characteristic that is a provider goes through it first): refused while it is open or a send is in progress (`ERR_PROV_IN_USE`), or when it is the board's own (`ERR_PROV_SYSTEM`); else it is taken out, so its id finds nothing, and its driver's `destroy` frees it. `sys_data_provider_mark_system()` (boot, after the drivers made theirs: the runIT pair, the LOGS characteristic, the UART console) makes every provider so far the board's own; a project's ERASE leaves them.

## UART

A UART provider is always **binary**, the console's port too, and between two boards on a cable at 2–5 Mbps:

- **A frame on the line**: COBS-encoded `[frame][crc16]`, ended by a 0x00 byte (COBS leaves no other 0x00). The receiver takes the bytes up to each 0x00 as one candidate: decoded, its CRC16-CCITT checked, then delivered whole. A corrupted byte fails the CRC and the frame is dropped (a request's source times out), so a broken byte between boards never runs a command. COBS adds at most one byte in 254.
- **The console's port**: the ROM bootloader's lines and a panic's text are no frames: they fail the CRC and are dropped by the board. The host's tool shows the bytes that are no frame as text, so the boot and a panic stay readable. After the start the log lines go only as frames, through the LOGS connector (`sys_log`'s hook of `esp_log`), never as text.
- **Between boards**: its own port, `WIRED`; the RX ISR in IRAM or RTS/CTS flow control, so a flash write (NVS) that stops the cache for milliseconds does not overrun the 128-byte FIFO at those rates; the TX from a task of high priority, so an answer does not wait behind the VM.

## Remote (`sys_data_provider_router`, root key `ROUTER_KEY_PROVIDER` 0x0D)

| Code | Command | Side | Request / answer |
|---|---|---|---|
| `0x01` | REMOVE | write | provider id |
| `0x02` | LIST | read | → `u8 count`, then `sys_data_provider_info_t` per provider (provider id, `u16` max frame now, `u8 state`: `STATE_SYSTEM`, `STATE_OPEN`, `STATE_FRAMED`, `STATE_WIRED`) |
| `0x03` | SEND | write | provider id, then the frame |

- The id is known before the driver answers (a UUID, a port), so a project's records name it in the commands after the create.
- SEND is send as a command, so the app or another board sends through a provider of this one. An open provider is its user's: SEND to it is refused (`ERR_PROV_IN_USE`).

## Limits

| Option | Default | Meaning |
|---|---|---|
| `CONFIG_SYS_DATA_PROVIDER_MAX` | 8 | provider slots, the board's own included |

## Proposed (does not exist yet)

- **MQTT link** (3, kept in the config): a driver (`sys_mqtt`) whose provider is a topic; its channel the topic's FNV-1a hash (32 bits), which the app computes the same way; two topics on one hash refused at add. The client (broker, login) is the driver's config, made by its own command; a topic's QoS and retain are set when the topic is made. The driver works on the `esp-mqtt` client's own task, so a topic costs no task:
  - `open` subscribes, `close` unsubscribes; the driver keeps its topics and subscribes them again after a reconnect.
  - `send` is `esp_mqtt_client_enqueue`: into the client's outbox, never waiting for the broker.
  - `MQTT_EVENT_DATA` (the client's task, the driver's RX task) comes in pieces for a long message: the driver joins them, up to `CMD_FRAME_MAX`, then `deliver`.
  - Broker lost: `set_max_frame(0)`, so nothing is sent into a growing outbox; connected again: the frame limit back.
- **UART create command**: opening a port as a provider (port, baud, pins, flow control).

Users: the session (SYS_SESSION.MD, `sys_session_attach`: a framed provider) and a connector's binding (SYS_DATA_CONNECTOR.MD "Bindings").

## Open

- The `.c` files: the registry (slots, lock, lookup by id), the router; the Kconfig.
- `sys_ble` adding a provider for a characteristic it creates, and removing it through the registry; its `send` with `NOWAIT` from another driver's RX task (UART's, when the session forwards).
