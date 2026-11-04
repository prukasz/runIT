# Data flow: rework

How data moves between the board's code, its links and the other boards, and the rework of it:
1. the connector became a stream (`sys_stream`) with one direction, that only pushes: in the firmware;
2. storage is the consumer's: the VM's input slots are to come;
3. a watch does not need a stream: in the firmware;
4. a remote device is an alias of another board's: in the firmware;
5. a raw provider can be bound to the router: to come.

This file is the one document of the changes to introduce: the picture across the components, what is left of the rework and its order, and what is designed but not started (Later). Items 1, 3 and 4 are in the firmware and their components' docs (SYS_STREAM.MD, SYS_WATCH.MD, SYS_SESSION.MD "Streams", SYS_REMOTE.MD, SYS_DEVICE.MD "Remote devices"); the app and the scripts do not follow them yet (Order).

## The layers

| Layer | What it is | Knows |
|---|---|---|
| **provider** | One way bytes go out and come in: a UART port, the runIT BLE pair, a user BLE characteristic. Transit buffers only (its TX queue, the frame being joined). | its link, nothing above |
| **session** | A peer on a framed provider: HELLO (version, id), PING, the dead-man, routing by board id, calls between boards, stream frames to peers. | peers and their ids; which session reaches which board |
| **router** | The board's commands: `[key][dir][code][args]` → a module, and its answer to whoever gave the command (`cmd_req_t` and its `done`). | modules, not frames, not providers |
| **stream** | A named stream (one byte) with one direction, OUT or IN: what goes in goes out to every target of its one list. | its targets: bindings to providers, subscribed peers, callbacks |
| **watch** | A read the board runs by itself on a trigger; its entries go to the one who added it. | the read, the trigger, the owner's id |
| **remote device** | A local id for a device of another board. | the board and the device's id there |

A provider has **one user**, one of three:

| User | For | What a message is | Answers go |
|---|---|---|---|
| a **session** | runIT peers: the app, a test host, another board | a frame `[frame][dst][src][seq][body]` | by the frame's `src`, through any session (forwarding included) |
| a **stream binding** | raw data: a user BLE characteristic, a raw UART | the payload as it is | none: a stream has no answers |
| a **command binding** (item 5) | a client outside runIT that sends a command and waits for its answer | `[tag][flags][key][dir][code][args]` | on the same provider, `[tag][status][data]` |

Commands go through a session or a command binding to the router; streams go through a stream to their targets. A frame of one never goes through the other.

## A command, end to end

```
 APP (F0) ══ BLE ══ MASTER (00) ══ UART link ══ SLAVE (01)
   session 1: app ↔ master      session 2: master ↔ slave
```

- Every session joins two neighbours on one link; its HELLO is its own (session 2 opens at boot, before the app comes). The header's `dst` and `src` are end to end.
- The app sends `[REQUEST][dst 01][src F0][seq]` on session 1. The master's session sees `dst` is not its own and forwards the frame as it is to the session whose peer said `01` in its HELLO (`session_find_by_peer_id`), without its router. The slave's session makes a `cmd_req_t {req, src F0, seq, flags}` and submits it to its router; the answer `[ANSWER][dst F0][src 01][seq]` goes back the same way.
- A client's id is its own: the app `F0`, another client `F1`..`FC`. Two sessions with one id: the second HELLO is refused (`ERR_SESSION_ID_TAKEN`), so a route never has two ways.
- No route to `dst` on the master: it answers itself, `ERR_SESSION_NO_ROUTE` (its `src` the master's, its `seq` the request's).

## A watch between two boards

```
 board A (owner)                                                      board B (runs the watch)
 watch client ── session ── provider ══ link ══ provider ── session ── router ── watch ──┐
      ▲                                                                   ▲              │ read
      │                                                                   └── device ◄───┘ (router_inject)
      └──────────── STREAM [0x80][owner][id][status][data] ◄──────────────── entry
```

How each side knows what a frame is:

1. **The session is opened once per provider**, by the HELLO of both sides (SYS_SESSION.MD "A session's life"). From then on a frame is routed by its `dst` board id; a module never sees the provider. A watch names a board, not a link: the session's route picks the provider (a slave reaches everything through its master).
2. **A watch is set up by a command**, not by a handshake of its own: A sends a REQUEST frame `[hdr REQUEST, dst B, src A, seq][0x0F WATCH][dir WRITE][ADD][head][read]`. B's router hands it to the watch router; the owner is the command's `src` (A). B answers it like any command.
3. **An entry is a STREAM frame**: the frame byte's kind (`SYS_SESSION_STREAM`) says "a stream, not a command", and the first byte of the body, the **stream id**, says which (The stream id byte): `0x80` is a watch's entry, a session channel; below `0x80` it is a stream's frame. A's session takes the channel frames itself (the watch client takes the `0x80` ones) and hands the others to the streams' listeners.
4. **Gone and reached**: B drops the watches of an owner its session reports gone; A's client adds its watches again when B is reached again (B restarted).
5. **The app's watch on a slave** is the same ADD with `dst` the slave: the master forwards the request, and the entries back (`[STREAM][dst F0][src 01]`), by `dst` without its router, and the owner is the app (`F0`). The app ends: its session ends on the master, which sends GONE `[F0]` to every slave, and they drop its watches. The slave restarts, or the link between them ends: the watches are gone there and nobody tells the app (GONE goes from the master to the slaves, not to the clients). **The app finds out by silence**: an ON_CHANGE watch sends an entry at least every `max_silence`, a PERIODIC one every `period`; none for longer than that (plus a margin) means the watch is gone, and the app sends its ADD again (an ADD of the same id replaces). An ON_EVENT watch has no silence to notice: the app checks it with LIST. An entry lost on the way is a gap in `seq`.

So there is no "watch hello" and no watch per provider: the session is the only handshake, the command is the setup, the stream id is the address.

## The stream id byte

The first byte of a STREAM frame's body names what the frame is. It is the stream's `id` (CREATE takes it, the frames of the stream carry it, a subscriber names it), except for the **channels** of the session itself:

| Range | What | Who takes it |
|---|---|---|
| `0x00` | invalid | nobody |
| `0x01`..`0x0F` | the board's own streams | `SYS_STREAM_TELEMETRY` 0x02, `_LOGS` 0x03, `_ERRORS` 0x04, `_EVENTS` 0x06 (made by `runit_board_cfg.c`) |
| `0x10`..`0x7F` | user streams | CREATE, from the app or the board config |
| `0x80`..`0xFF` | session channels | `SYS_SESSION_CHANNEL_WATCHES` 0x80 (a watch's entry, to its owner), `SYS_SESSION_CHANNEL_REMOTE` 0x81 (a remote device's events and errors); the rest is reserved |

- **A channel is not a stream**: no queue, no bindings, no subscribers. A module sends its frame to the one peer it names (`sys_session_stream`), and the session of the board it reaches takes it (`sys_session_on_stream`): bit 7 set is a channel, else the streams' listeners get it. No stream can take a channel's number, and a channel has no stream's commands.
- CREATE refuses an id of `0x80` or more (and `0x00`) as a bad argument.
- The streams' numbers are in the stream's config, the channels' in `sys_session_config.h`; both are published to the app (`generated/`), each as its own enum.

## The rework

### 1. The connector is a stream, and only pushes

In the firmware: `components/sys/sys_stream` (SYS_STREAM.MD). As the diagram's page 7 has it (`runit-dataflow.drawio`, "Stream"): a stream has an id and one **direction**, set at CREATE: OUT (the board sends: `sys_stream_send`) or IN (what comes in on its providers). Its targets are **one list** of three kinds: a provider (a binding: OUT sends there, IN takes from there), a peer (a subscriber, by its session) and a callback (the board's code, on the stream task). No stream both ways, so nothing goes back to its source and there is no loop: a device both ways is two streams, and a bridge (a GPS on a raw UART to the app) is an IN stream with an RX binding and the app subscribed. A stream keeps nothing; SUSPEND / RESUME are gone (to stop a stream, its targets go).

### 2. Storage is the consumer's

- The VM latches its inputs at the start of a scan (as a PLC): a VM listener writes a stream's message into the VM's input slot; the scan reads the slot. One slot per input keeps the latest; a queue, where a block needs every message, is the VM's.
- The app keeps what it shows: it subscribes and gets every frame as it comes.
- The watch client's copies (item 4) are the same thing: the reader's storage, kept by the reader's board.

### 3. A watch without a stream

In the firmware (SYS_WATCH.MD "Entries"): the head without `connector` (11 B), the entries in the session channel `SYS_SESSION_CHANNEL_WATCHES` (0x80).

### 4. A remote device is an alias

In the firmware (SYS_REMOTE.MD, SYS_DEVICE.MD "Remote devices"): the record `{board, remote_id, flags: WIRED}`, a wired device's calls waited for and a not wired one's read from a watch's copy (`CONFIG_SYS_REMOTE_WATCH_MS`), `ERR_REMOTE_NOT_WIRED` once per board, the subscription to the device (`ROUTER_KEY_REMOTE` 0x10, the channel `SYS_SESSION_CHANNEL_REMOTE` 0x81, its own queue and task), `ERR_REMOTE_LOST` for a gap; the mirrors, `EVENTS_ALL` and `SYS_EVENT_LOCAL` are gone. The VM's device block takes `ERR_WATCH_STALE` as "later" too.

### 5. A raw provider bound to the router

For a client outside runIT (Node-RED, Home Assistant, a script) that drives a device or resets the board with one command and waits for its answer, without a HELLO: a provider's third user, the **command binding**.

```
 provider ──► command binding ──► router_submit (cmd_req_t: src NONE) ──► module
    ▲                                                                        │
    └──────── [tag][status][data or the error record] ◄── done (ctx: the provider)
```

- **In**: one message is one command, `[tag][flags][key][dir][code][args]`. `tag` is the client's own (its `seq`), sent back with the answer; a client that waits for each answer may keep it 0. `flags` are `ROUTER_FLAG_*`: a STOP from outside goes URGENT, a command with no use for its answer NOREPLY.
- **Out**: `[tag][status][data or the error record]` (`cmd_err_record`), on the provider the command came from: the answer is routed by the provider, not by an id, so the client needs no session.
- **This board only**: no `dst`, nothing forwarded (with a `dst` it would be a session).
- **The command's `src` is `SYS_SESSION_ID_NONE`** (0xFD): no peer. The router and the error record take it as any byte. What the board keeps for a peer refuses it, each with an error of its own:
  - WATCH ADD, STREAM SUBSCRIBE, REMOTE SUBSCRIBE (each refuses NONE already);
  - ACTIONS RECORD (`sys_actions_record_start` keeps the `src` as the recording's owner: a check to add);
  - SYS DEADMAN set: it is the board's, and fires when a client's session ends; a client with no session would be unprotected without knowing it (a check to add).

  A client that follows a value reads it again, or takes a stream on a stream binding.
- **Made** by the board config (C) or a command of the provider router (`BIND_COMMANDS`, `UNBIND`): a provider another user has is `ERR_PROV_IN_USE`, as for a stream binding.
- **Code**: a small component of `sys` (`sys_cmd_bind`): the provider's rx → a `cmd_req_t` → `router_submit` with its own `done` → the provider's send. The router does not change: it knows `cmd_req_t` and `done`, not providers (so the binding is not in `core/router`).
- **Who may send**: whoever reaches the provider.

## What changes on the wire

Items 1, 3 and 4 changed the wire in the firmware; outside it nothing follows them yet:

- **STREAM** (was CONNECTOR, the same key byte 0x04; `contract_stream.json`, the enums' prefixes `SYS_STREAM_`):
  - CREATE `{u8 id, u8 dir, u8 flags}`; the id `0x01`..`0x7F` (the board's own `0x01`..`0x0F`, user streams `0x10`..`0x7F`), from `0x80` refused;
  - BIND `{u8 id, provider id}`, without flags (the direction says which way);
  - LIST: per stream its head `{cfg, u8 state, u8 targets}`, then its targets `{u8 type, u8 peer, provider id, u16 dropped}`;
  - SUSPEND, RESUME, READ and MIRROR gone (codes 0x05, 0x06, 0x08, 0x0B unused); `EVENTS_ALL` (0x07) gone;
  - error tags `ERR_STREAM_*` (module 5), `POOL_FULL`, `NO_RX`, `MIRROR_LOST` gone, `ERR_STREAM_DIRECTION` new.
- **WATCH**: the head without `connector` (11 B; LIST's row 13 B); its entries in the session channel `0x80`.
- **DEVICE**: CREATE_REMOTE's record `{u8 board, u16 remote_id, u8 flags}` (4 B); `ERR_DEV_REMOTE_NOT_WIRED` gone.
- **REMOTE** (new, key 0x10): SUBSCRIBE / UNSUBSCRIBE of a device (`[id u16]`), its events and errors as a STREAM `[0x81][id u16][kind][data]`, `seq` the subscription's; `ERR_REMOTE_NOT_WIRED`, `_LOST`, `_SUBS_FULL`, `_ALIASES_FULL`.
- **SESSION**: the STREAM frame's first body byte is the stream id; from `0x80` a session channel: `SYS_SESSION_CHANNEL_WATCHES` 0x80 and `_REMOTE` 0x81.
- **Outside the firmware** (the generated bundle is regenerated):
  - the app: the backend's stream (was connector), watch and device code, the views;
  - the scripts: `router_tests.py` (streams), `remote_tests.py` (connector rx, the mirrors), `forward_tests.py`, `tree_tests.py`;
  - item 5 still changes **PROVIDER**: `BIND_COMMANDS`; the command binding's message and answer.

## Order

Each step builds and passes its scripts on its own:

1. **The app and the scripts follow items 3, 4 and 1**: the wire above, then the suites on the boards.
2. **Item 5**, the command binding: new, touches nothing above.
3. **Item 2**, the VM's input slots: a VM callback (`sys_stream_listen`) per stream the program reads, with the VM's stream blocks.

## Later

Designed, not started:

- **UART create command**: opening a UART port as a provider with a command (port, baud, pins, flow control), for a raw UART as a stream binding (item 1) or a command binding (item 5).
- **The app told of a slave's restart**: now the app finds out by the silence of its watches (A watch between two boards, point 5); a message from the master to its clients when a slave's session ends or restarts would tell it at once, and would cover the ON_EVENT watches.
