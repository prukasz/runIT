# sys_data_connector — connectors (draft)

Component `components/sys/sys_data_connector/` (draft in `_wire_draft/`, not built). A **connector** is a named stream of data between the board's code and the outside: logs, errors, telemetry, events, a VM stream, an MQTT topic, a bridge between two ports. Code knows a connector by its **id**, one byte: it sends to it and reads from it. Producers never block; readers read what came in.

A connector has two kinds of **targets**, and what a frame looks like follows from the target, not from the connector:

| Target | Made by | A frame goes as | Comes in |
|---|---|---|---|
| a **binding** to a provider (SYS_DATA_PROVIDER.MD) | BIND (the board config, the app) | the payload as is: the provider is the connector's alone, so it names the stream | yes, `BIND_RX` |
| a **subscriber**: a peer (the app, another board) | SUBSCRIBE, by the peer itself | `[connector][payload]` in a session's STREAM frame (SYS_SESSION.MD "Streams"): the byte tells the peer's streams apart | no |

Commands and their answers do not go through connectors: they are the session's, which opens its own provider.

## Files

| File | Role |
|---|---|
| `sys_data_connector.h` | Use: init, bind, subscribe, mirror, send, max payload, rx seq, read, notify, is-task |
| `config/sys_data_connector_config.h` | What a connector's id, settings, bindings and state are: the system connectors, rx, the binding, connector and state flags (published to the app) |
| `internal/sys_data_connector_internal.h` | For its router only: the connector (`sys_data_connector_t`), create, remove, unbind, suspend, list |
| `router/sys_data_connector_router.h` | The remote interface (root key `ROUTER_KEY_CONN` 0x04) |
| `errors/` | `ERR_SYS_DATA_CONNECTOR_MAP`, its registration, the messages |

## A connector

What it is, set at create and never changed:

| Setting | Values | Meaning |
|---|---|---|
| id | 0x01..0xFF | Its byte, the first of its frames to a subscriber; 0x00 is invalid, 0x05 (`WATCHES`) the watches' |
| rx | `NONE`, `LATEST`, `FIFO` | What it keeps of what comes in on its rx bindings (Receiving) |
| `FLAG_URGENT` | flag | Its frames go to the front of every provider's queue (Sending) |
| `FLAG_FORWARD` | flag | What comes in on one binding goes out to every other target (Forward) |
| `FLAG_TX_LATEST` | flag | One frame waits at most: a newer one replaces it (Sending), the mirror of rx `LATEST` |
| rx size | bytes | `LATEST`: the largest message kept; `FIFO`: the ring; else 0 |
| tx size | bytes | `TX_LATEST`: the largest frame kept waiting; else 0 |

`sys_data_connector_cfg_t` holds them in 7 bytes: `id`, `rx`, `flags`, `rx_size`, `tx_size`. Both buffers come from one pool, `CONFIG_SYS_DATA_CONNECTOR_BUF_POOL`.

There are no roles: what a connector does follows from these and its targets.

| Use | rx | Targets | forward |
|---|---|---|---|
| system stream (logs, errors, telemetry, events) | none | subscribers; a provider of its own (the LOGS characteristic) | – |
| VM stream out | none | subscribers, or a tx binding | – |
| VM stream in, MQTT topic, user characteristic | latest | an rx + tx binding | – |
| bridge (a GPS, an AT modem, a foreign controller) | none | rx + tx bindings on both sides | yes |
| bridge with a look at it | none, fifo or latest | rx + tx bindings on both sides; the app subscribed | yes |

**System connectors** (`sys_data_connector_init`): `TELEMETRY` 0x02, `LOGS` 0x03, `EVENTS_ALL` 0x07 normal; `ERRORS` 0x04, `EVENTS` 0x06 urgent; send only. They are not removed and their settings are fixed; the app and other boards subscribe to them, and the board config binds LOGS to the LOGS characteristic (0xFFE3), where its lines go as they are.

The two event connectors carry the same frame, `[ROUTER_KEY_EVENT][SYS_EVENT_OP_NOTIFY][sys_event_t]` (SYS_EVENT.MD), from `sys_event_publish`:

| Connector | Events | Who subscribes |
|---|---|---|
| `EVENTS` | the NOTIFY ones: what matters to the user (a fault, an over-current) | the app |
| `EVENTS_ALL` | every event: a pin's edge, an ADC window, a device's own | another board (its mirror there: the events of its remote devices, for its VM's `ON_EVENT`), the app's debug view |

Without a subscriber `EVENTS_ALL` costs a publish nothing: a send without a tx target is dropped before the queue. It is not urgent, so a burst of events (a fast pin) never goes before the answers to commands, and a full queue drops events there only (its `dropped`; a subscriber sees the gap in `seq`). An event with `SYS_EVENT_LOCAL` in its events table (a fast source, what only an inline listener of this board wants) never goes there: it stays on this board.

`WATCHES` (0x05) is no connector: a watch sends its entries straight to its owner, with no subscription, in STREAM frames that start with this byte (SYS_WATCH.MD "Entries"). So no connector takes it: CREATE of 0x05 is `ERR_CONN_EXISTS`, as of a system connector's byte.

## Bindings

`sys_data_connector_bind(id, provider, flags)`: `BIND_RX`, `BIND_TX` or both; again on the same provider changes the flags. The binding opens the provider as its **one user** (SYS_DATA_PROVIDER.MD "Use"): no one else sends on it or takes from it, so bytes of two senders never mix on a line, and a provider in use can't be removed. A provider another user has: `ERR_PROV_IN_USE`. Unbind and remove close the providers.

A frame goes to a binding as it is: no byte before it, since the provider carries this connector only.

## Subscribers

A peer **subscribes** itself: SUBSCRIBE `u8 id`, and the command's `src` (`router_origin()`) is the subscriber, so no peer names another. From C, `sys_data_connector_subscribe(id, peer)` (the board config); a board that wants another board's stream makes a mirror of it (Mirrors).

- Every frame goes to every subscriber as a STREAM frame of the session, `[connector][payload]`, with `seq` the connector's frame counter (a gap is frames lost) and its URGENT flag (`sys_session_stream`, `NOWAIT`). Busy, down or no route: `dropped` + 1, as on a binding.
- SUBSCRIBE of the same peer again gives one subscription, so a subscriber that restarted and subscribes again gets one stream, not two.
- A subscription ends with UNSUBSCRIBE, or when the peer is gone: the connectors listen for gone peers (`sys_session_on_gone`) and drop its subscriptions. A subscriber that wants the stream again subscribes again.

## Mirrors

(Built in the component without the rx buffers: a mirror has a listener and its targets, and keeps nothing; the rx kinds below, forward and coalescing are not built.) A **mirror** is this board's connector for a connector of another board: what that board sends there comes in here, and goes on to every target of the mirror. So any number of this board's users (sys_remote for remote devices' events and errors, a binding to an MQTT topic, the VM reading its `rx_seq`, the app subscribed to the mirror) get one stream of another board, which crosses the link once.

- `sys_data_connector_mirror(id, board, remote_id)`: makes the connector `id` (created before, with rx `LATEST` or `FIFO`) the mirror of `remote_id` on `board`, and subscribes this board there (SUBSCRIBE, a call: SYS_SESSION.MD "The board's own calls").
- A STREAM frame `[remote_id][payload]` from `board` (the connectors' stream listener, SYS_SESSION.MD "Streams") goes into the mirror's rx as if it came on a binding: kept by its rx, the listener told, forwarded to the mirror's tx targets. Its `from` in READ is no provider: `{link 0, channel board}`.
- A gap in the frame's `seq` is frames lost on the way: the mirror's `missed` grows by it, and `ERR_CONN_MIRROR_LOST{connector, board, count}` is reported as an error of that board (`ERR_RECORD_SRC_BOARD`, `_wire_draft/payloads.h` "errors, runit"), once per gap.
- The board gone (`sys_session_on_gone`): the mirror is down (its listener told, `seq` unchanged); reached again (`sys_session_on_reached`): it subscribes again.
- `sys_data_connector_unmirror(id)`: UNSUBSCRIBE there; the connector stays, as an ordinary one.

## Sending

One connector task sends for every connector, and a provider's driver works on its own task (`sys_ble`'s, the `esp-mqtt` client's): a connector or a provider costs no task, only its slot and its buffers. A frame is copied once into the connector queue and handed on from there; the queue is what keeps `send` from ever waiting on a driver (a producer may hold a driver's lock, or be the log hook inside one), keeps one connector's frames in order, and carries the coalescing and the fan-out to several targets.

`sys_data_connector_send(id, data, len)` **never blocks**: it copies `[connector][payload]` into the queue and returns. The task sends it to every tx target straight from the queue (a binding gets the payload, a subscriber the whole), then gives the room back.

- Suspended, or no tx target: dropped, NULL. Queue full: `ERR_CONN_QUEUE_FULL` (the log and error paths drop it silently).
- A payload longer than `CMD_DATA_MAX − 1` (CMD.MD "Frame size") is refused at send (`ERR_CONN_TOO_LONG`). Per binding the provider checks its own `max_frame`: too long, `ERR_PROV_TOO_LONG`, not sent there, never cut.
- `sys_data_connector_max_payload(id)`: the smallest of `CMD_DATA_MAX − 1` (when it has subscribers) and the `max_frame` of each tx binding that is up; producers size their frames by it (whole entries per frame). It reads the providers' fields, so any task may call it.
- **Urgent** (`FLAG_URGENT`): every send of its frames goes to the front of the driver's queue (`SYS_DATA_PROVIDER_SEND_URGENT`, the STREAM frame's URGENT flag). The connector queue itself is never long, since the task waits on no driver.
- **Coalescing** (`FLAG_TX_LATEST`): the frame waits in the connector's slot (`tx size`), and only a mark of it (its id) in the queue; a send while it waits replaces the slot and queues nothing. The task sends what the slot holds when it comes to the mark, so under a slow link the newest goes, not a backlog of old ones. Only for frames that each carry the whole state (a set of values, a position): a frame that carries a part loses the part replaced. Longer than `tx size`: `ERR_CONN_TOO_LONG`.
- Any number of senders: frames are whole and never interleave; one sender's frames keep their order, two senders' frames mix in any order.
- Errors in the task are logged only (`err_log`): reporting them would queue an error record back into this task.

### The task

```
for (;;) {
  wait for a notification: QUEUE (a frame queued), RX (something came in), STATE (a provider up or down)
  1. every frame queued, in order: to every tx target of its connector, NOWAIT;
     a coalescing mark: the slot's frame, taken under the lock, sent, the slot freed
  2. RX / STATE: the listener of every connector marked (set_notify)
}
```

- **It never waits on a driver.** Every send is `NOWAIT`: a provider whose queue is full answers `ERR_PROV_BUSY` at once, and the frame is lost on **that target only** (its `dropped` + 1); the other targets and connectors go on. So one slow provider (a full UART console) never holds the streams of the others.
- A target's `dropped` counts every frame not sent to it: provider busy, down, the frame too long for it, no route to a subscriber. LIST shows it; it is logged once, then again after the target has taken a frame.
- No timers: the task sleeps until notified. Forward needs no path of its own: the rx callback queues the frame as `send` does.
- Priority 4, below both router tasks (work 5, urgent 15): streams wait, commands don't. The listeners run here, so the stack (`CONFIG_SYS_DATA_CONNECTOR_TASK_STACK`) holds them, and a listener stays short.

## Receiving

What comes in on an rx binding arrives on the provider driver's RX task: the connector copies it into its buffer there (under its lock, a copy only), counts it (`seq`) and wakes the connector task, which tells the listener. Nothing else runs on the driver's task.

| rx | Buffer | Readers | Full |
|---|---|---|---|
| `LATEST` | one message of `rx size` | any: each keeps the last `seq` it saw | the new one replaces it (`missed` + 1 when not read) |
| `FIFO` | a ring of `rx size` bytes | one: a read takes the oldest out | the new one is dropped, `missed` + 1 |

- `sys_data_connector_rx_seq(id)`: the count so far, no lock: the VM compares it each scan and reads only when it changed.
- `sys_data_connector_read(id, buf, cap, &msg)`: the bytes and `{seq, len, missed, from, cut}`; nothing there: `msg.len` 0. A message longer than `cap` is cut to it (`cut`), the rest is dropped (a FIFO message is taken out whole).
- `sys_data_connector_set_notify(id, fn, ctx)`: one listener, on the connector task, after the message is in the buffer; also when a provider of it goes up or down (`seq` unchanged). For the firmware's own code.
- A message longer than `rx size`: kept cut, `cut` set at its read.

## Forward

A connector with `FORWARD` sends what comes in on one rx binding to every other tx target (never back to the binding it came on), through the queue, as `send` does: rx A → tx B and rx B → tx A makes a transparent bridge, and a subscriber sees both ways. With rx `NONE` nothing is kept; with `LATEST` or `FIFO` it is kept as well, so code sees what passes. A frame longer than the other side's `max_frame` is not sent there (`ERR_PROV_TOO_LONG`, logged): on a framed provider frames are whole, so the two sides' limits must match.

A bridge is for what is no runIT board. A runIT board is reached through its session (SYS_SESSION.MD): configuring it, updating its firmware (OTA commands), its streams (subscribing to them); its first flash is the board's own flasher's, which drives its EN and IO0 pins.

## Remote (`sys_data_connector_router`, root key `ROUTER_KEY_CONN` 0x04)

| Code | Command | Side | Request / answer |
|---|---|---|---|
| `0x01` | CREATE | write | `sys_data_connector_cfg_t` |
| `0x02` | REMOVE | write | `u8 id` |
| `0x03` | BIND | write | `u8 id`, provider id, `u8 flags` |
| `0x04` | UNBIND | write | `u8 id`, provider id |
| `0x05` / `0x06` | SUSPEND / RESUME | write | `u8 id` |
| `0x07` | LIST | read | → `u8 count`, then per connector `sys_data_connector_head_t` (`cfg`, `u8 state`: `STATE_SYSTEM`, `STATE_SUSPENDED`, `u8` subscribers, `u8` bindings), its subscribers ({`u8 peer`, `u16 dropped`}), its bindings ({provider id, `u8` flags, `u16 dropped`}) |
| `0x08` | READ | read | `u8 id` → `sys_data_connector_msg_t` (14 B: `seq`, `len`, `missed`, `cut`, `from`), then the bytes (as `sys_data_connector_read`) |
| `0x09` | SUBSCRIBE | write | `u8 id`: the command's `src` gets the connector's frames (Subscribers); the board itself (`SYS_SESSION_ID_SELF`): `ERR_CONN_BAD_ARG` |
| `0x0A` | UNSUBSCRIBE | write | `u8 id`: the command's `src` |
| `0x0B` | MIRROR | write | `u8 id`, `u8 board`, `u8 remote_id`; `remote_id` 0: the mirror ends (Mirrors) |

## Errors (`ERR_MOD_SYS_DATA_CONNECTOR` = 5)

`ERR_CONN_NOT_FOUND{connector}`, `_EXISTS{connector}`, `_SYSTEM{connector}` (remove: a system connector), `_TARGETS_FULL{connector}` (bind, subscribe), `_TOO_LONG{connector, len, max}` (the connector's own limit), `_QUEUE_FULL{connector, len}`, `_BAD_ARG`, `_FULL{connector}`, `_POOL_FULL{connector, size, free}`, `_NO_RX{connector}`, `_MIRROR_LOST{connector, board, count}`. A provider's failure (open, send) is its own `ERR_PROV_*`, which names it. Codes 1, 5 and 10 are unused.

## Limits

| Option | Default | Meaning |
|---|---|---|
| `CONFIG_SYS_DATA_CONNECTOR_MAX` | 16 | connector slots, the 5 system ones and the mirrors included (sys_remote makes two per board that has a remote device here) |
| `CONFIG_SYS_DATA_CONNECTOR_BINDINGS` | 4 | bindings per connector |
| `CONFIG_SYS_DATA_CONNECTOR_SUBSCRIBERS` | 4 | subscribers per connector |
| `CONFIG_SYS_DATA_CONNECTOR_QUEUE_SIZE` | 4608 | bytes of the queue |
| `CONFIG_SYS_DATA_CONNECTOR_BUF_POOL` | 2048 | bytes the rx buffers and coalescing slots of every connector take from |
| `CONFIG_SYS_DATA_CONNECTOR_TASK_PRIO` / `_STACK` | 4 / 4096 | the connector task |

## Proposed (does not exist yet)

- **sys_event**: every event to `EVENTS_ALL` but those with `SYS_EVENT_LOCAL`, the NOTIFY ones to `EVENTS` as well (SYS_EVENT.MD "Two ways out" gains the first; the events tables gain the flag).

## Open

- The `.c` files: the registry, the queue and the task, the pool with the rx buffers and the coalescing slots, the subscribers, the router.
- The board config: LOGS bound to the LOGS characteristic; the UART console's client subscribing to LOGS and ERRORS once its session opens (before, the boot's lines are text the host's tool shows: SYS_DATA_PROVIDER.MD "UART").
