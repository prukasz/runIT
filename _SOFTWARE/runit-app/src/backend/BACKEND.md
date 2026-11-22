# The backend: the link to a board (for agents)

`runit-app/src/backend/`: requests, the board's streams and a connection that keeps itself up, over BLE. No UI, no DOM beyond the Web Bluetooth platform. Written from the firmware (`runit-esp/components/core/cmd/` CMD.MD "The header", `sys/sys_session/`, `sys/bus/sys_ble/`, `providers/provider_ble/`, `app/runit/` RUNIT.MD "The board's data") and `generated/`.

```ts
const platform = new WebBluetoothPlatform(bundledLinkDefinitions())
const link = new RunitLink(platform)
await link.connect(await platform.requestDevice())            // a user gesture; or platform.knownDevices() with no chooser
link.onStatus((s) => …)                                       // { state, reason?, attempt? }
link.onStream((f) => …)                                       // a PUSH: { key, name, src, seq?, urgent, data, at }
const services = new Services(link)
const { value } = await services.call('SYS', 'PING')          // { uptime_ms: 1234 }
await services.device(0, 'IO', 'TOGGLE', { pin: 4 })           // device 0 (the ESP32-S3's GPIO), contract IO
await services.createProvider(8, 'ble', { svc: 0xee00, chr: 0xee01, name: 'temp' })   // a characteristic of the board's GATT table
```

**Everything is the firmware's.** Every number of the link is a symbol of `generated/common/enums.json`, every command, its op, its side, its fields and its answer a descriptor's (`generated/services`, `contracts`, `devices`, `providers`), and `definitions.ts` is the only file that reads `generated/`. A changed op number, field, struct layout, answer shape, module number, UUID or flag is picked up by `sh templates/sync.sh` with nothing to edit here. What the code names is a descriptor's id (a service, a command, a field), each in one constant per module (`SESSION_COMMANDS`, `WATCH_COMMANDS`, `WATCH_HEAD`, `DEADMAN_COMMANDS`, `ACTIONS_COMMANDS`, `PROVIDER_COMMANDS`, `BLE_WAYS`, `EVENT_LAYOUT`, `STREAM_MODULES`), held against the catalog by a test. A symbol the firmware stops publishing stops the load with a message that names it; a descriptor that does not hold together is a `catalog.problems` entry.

## Layout (dependencies point down)

| Module | What it is |
|---|---|
| `definitions.ts` | `LinkDefinitions` from `enums.json`: the module numbers (`keys`: `sys`, `session`, `sys_device`, `watch`, …), the direction byte (`dir`, `cmd_dir_e`), the header's bits, kinds and sources (`type`, `kind`, `src`: `cmd_type`, `cmd_kind_e`, `cmd_src`), the session's control ops and key (`control`: `sys_session_control_e`, `MODULE_ID_SESSION`), each stream's key (`streams`, by `STREAM_MODULES`: `errors` `MODULE_ID_ERRORS`, `events` `MODULE_ID_SYS_EVENT`, `logs` `MODULE_ID_LOG`, `watches` `MODULE_ID_WATCH`, `remote` `MODULE_ID_REMOTE`), the router's flags and status, the runIT GATT UUIDs (`gatt`: `ble_runit_uuid`), the fragment bits (`frag`: `ble_frag`), the GATT table's and a BLE provider's flags (`ble`: `ble_svc_flag`, `ble_chr_flag`, `provider_ble_flag`, `cmd_send_flag`), the error record's header (`err_record_*`), the watch tick (`watch_tick`), `CMD_FRAME_MAX` and `CMD_HEAD` (`cmd_frame`); `bundledServiceCatalog` (a glob over the four descriptor folders: a new file is picked up), `errorCatalog` (`errors.json`), the onboard action ids. |
| `catalog/` | `services.ts`: `ServiceCatalog`, every command of every router, device contract and device type, and every provider type (`providers`, `byProviderType`: its type number and config), as the descriptors describe them (a device type also its pins, channels, presets and ranges), and what does not hold together (`problems`). `codec.ts`: a payload from field values and back, by the descriptor's layout. |
| `protocol/` | `frame.ts`: the header of every frame (`encodeFrame`, `decodeFrame`), a request (`Request`: key, body), `encodeRequest`, a stored frame (`requestFrame` `[key][body]`, `frameRequest` back), the control frames (`encodeControl`, `encodeHello`, `decodeHello`). `calls.ts`: the request of any catalog command (`serviceCall`, `deviceCall`, `createDeviceCall`, `createRemoteDeviceCall`, `createProviderCall`), a stored frame read back (`readDeviceFrame`, `readProviderFrame`), the answer decoded (`decodeAnswer`, `decodeReturns`, `LeadAnswer`). `gatt.ts`: a BLE way (`bleWayCall`, `readBleWay`) and the board's GATT table (`bleTableCall`, `decodeBleTable`). `errors.ts`: the error record. `events.ts`: `decodeEvent`, a `sys_event_t` with its contract and its row of that contract's `events` table. `watches.ts`: `WATCH ADD` / `REMOVE` and a watch's entry. |
| `errors/` | `interpret.ts`: `ErrorInterpreter`, an error record read for a user (messages, the instances it is about as `Ref`s). `program.ts`: `programRefs` / `deviceRefs`, the app's names for those instances. |
| `transport/` | `types.ts`: what a BLE platform gives (`requestDevice`, `knownDevices`, `forget`, `open(device, spec, handlers)` → a channel to write packets to, packets and a loss notice back; `OpenSpec`: the service, the written characteristic, the heard ones and the optional ones). `framing.ts`: a frame to packets and back (SYS_BLE.MD "Framing"). `webBluetooth.ts`: the Web Bluetooth platform. |
| `session/` | `commands.ts`: `CommandClient`, a u16 seq per request, its answer by seq and key, timeouts, pipelining, urgent first. `link.ts`: `RunitLink`, the connection, the session, the streams. `services.ts`: `Services`, the catalog's commands over the link. `sessions.ts`: `Sessions`, the board's id and its sessions (`decodePeers`). `watches.ts`: `Watches`. `deadman.ts`: `DeadMan`. `records.ts`: the board's records (SYS_ACTIONS.MD). `errorlog.ts`: `ErrorLog`. |

## The frame

Every frame on the line has the header of CMD.MD, `CMD_HEAD` bytes (`protocol/frame.ts`; `protocol.test.ts` holds the field order to `cmd_hdr_t`):

```
[type u8][src u8][key u8][seq u16 LE][data]
 type: the router's flags (urgent, no reply), an answer's status (ERROR), the kind (request, answer, push, control)
 src:  who made it: this client's id on what it sends, the board's (or another board's it passes on) on what it gets
 key:  a module's number: a request's router, its answer's, a PUSH's stream, MODULE_ID_SESSION on a control frame
 seq:  a request's counter, echoed by its answer; a PUSH's count of its class (normal, urgent) on the session; 0 on control
```

A request's data is its body, `[dir][op][args]` (`[dir][id u8][(contract << 8) | op u16][args]` for an addressed router: the devices, the providers); a stored frame (a record's, a watch's read, an error record's request) is the key then the body, `requestFrame`. An answer's data is the router's output, or the error record when the status is ERROR. A control frame's data is `[op][...]`: HELLO `[HELLO][boot_id u32][build_id u32]` (`sys_session_hello_t`, the sender's id is the header's `src`), PING, NO_SESSION.

## Every command, from the descriptors

How an entry is addressed follows from what its descriptor has:

| Entry | Has | Request (key, body) |
|---|---|---|
| service | `router_key` (`generated/services`) | its key, `[dir][op][args]` |
| service addressed by an id | `router_key` and `contract_id` (PROVIDER) | its key, `[dir][id u8][(contract id << 8) \| op u16][args]`; the id is the provider's, `id` of the call (left out: `SYS_DATA_PROVIDER_ID_NONE`, LIST) |
| device contract | `contract_id`, `scope: device` (`generated/contracts`) | the device contract's key, `[dir][device u8][(contract id << 8) \| op u16][args]` (SYS_DEVICE.MD) |
| device type | `type_id` (`generated/devices`) | its own ops in contract `SYS_DEVICE_CONTRACT_OWN`; CREATE (the device contract's write) carries `[type u16][flags u8][config]` (flags: `SYS_DEVICE_CREATE_SYSTEM`, the board's own) |
| provider type | `kind: provider-definition`, `type` (`generated/providers`) | PROVIDER CREATE under the provider's id: `[type u8][flags u8][config]`, the config by the descriptor's fields |

A contract id and a router key are module numbers; the device contract is the one that is both (`catalog.lifecycleContractId`).

**A command has a read side, a write side or both** (`CommandDef.read`, `.write`: each its own `args` and `returns`), and every request says which with its first byte (`dir`). A command with both is a mirror: its read takes the key (a pin, a channel; nothing for a module's setting) and answers what its write takes. A call of a command with one side runs it; one with both is told which (`side`, else it throws).

```ts
await services.call('SYS', 'DEADMAN', { timeout_ms: 500 }, { side: 'write' })
await services.call('PROVIDER', 'REMOVE', undefined, { side: 'write', id: 8 })   // a service addressed by an id
await services.device(0, 'IO', 'LEVEL', { pin: 4, level: 'High' }, { side: 'write' })   // a choice by its label, an enum by its member name
await services.device(9, 'dc_motor', 'speed', { speed_x10: 90 }, { side: 'write' })     // a device type's own op (a field's `scale`)
await services.create(20, 'dc_motor', { bridge: 5, channel: 0, … })              // the fields of device_dc_motor.json's config; a pin is { device, pin }
await services.createRemote(20, 'esp32s3_gpio', 2, 0)                            // device 20 here is an alias of device 0 of board 2; 'wired' for a cable
readDeviceFrame(catalog, frame, (id) => typeOfDevice(id))                        // a stored frame read back: { device, entry, command, side, args } or { …, create }
```

- **Layout**: a field sits at its `offset` (or after the one before), is `size` bytes (or its `raw_type`'s width); a number field of several widths is an array; a `bytes` / `string` field without a size runs to the end and is last; a `string` of a size is NUL-padded, one without ends in one NUL. Little endian.
- **Values** are given by field id. A missing field takes its `default`, an optional one zero; else the call throws `PayloadError`, as it does for a field id that is not there, a value outside the raw type or the descriptor's `min` / `max`, and an unknown choice label or enum member. A pin reference (`pin_ref`, `sys_io_pin_t`: device u8, pin u8, or a list of them) is `{ device, pin }`, `null` for an unused pin (`io_pin_none`). A bitmask takes its number. A command marked `urgent` is sent urgent. `CommandDef.recordId` is the settings record its write is kept in; `CommandDef.tags` the descriptor's (`app`: the app sets it itself).
- **Answers** are decoded by the side's `returns`: a struct by field id (booleans as booleans, pins as `{ device, pin }` or `null`), a value, or a list of either. An answer with a `lead` (a number then rows to its end: SESSION PEERS, PROVIDER LIST, ACTIONS LIST) is `{ lead, rows }`. An answer the descriptor gives only in words (BLE LIST: lists of lists, NUL-ended names) is its bytes (`Answer.data`); BLE LIST has `decodeBleTable`.
- **Problems**: overlapping fields, a field that does not fit its raw type, a router key or contract id that is no published module, an op twice, a command with neither side, a device naming a contract nobody describes, a pin reference not a multiple of `sys_io_pin_t`, a lead without rows, a provider type that is no member of `sys_data_provider_type_e`, an unpublished symbol the catalog needs. They go to `catalog.problems`; the rest stays usable. `services.test.ts` holds the bundle at none, and every layout of a packed C struct (in a `.h` or a driver's `.c`) against the struct.

## The connection

- **Opening is one platform call** (`OpenSpec`): the runIT service alone, written and heard on its session characteristic (`gatt.session`, framed) and, when the board has them, on the raw ones: the log lines (`gatt.logs`) and the watches' entries (`gatt.telemetry`). The browser runs one GATT operation at a time: they go through one chain.
- **Then the session** (SYS_SESSION.MD "A session's life"): the link sends HELLO with this client's id as the source (`LinkOptions.clientId`, by default the first client id; one of `src.clientFirst..clientLast`, no two open sessions of a board have the same) and a boot id of its own (random per link), again every 0.5 s until answered; the board's HELLO gives its id (its source), boot id and build (`link.session`: `clientId`, `boardId`, `bootId`, `buildId`); then SESSION PEERS gives this session's dead-man (`deadmanMs`, 0: none). Only then is it `connected`. **Nothing is subscribed**: the board links its errors and events to the session, its log lines and watches' entries to their characteristics (RUNIT.MD). With the dead-man on, something goes to the board at least every third of its time (a PING when nothing else does). A NO_SESSION or a HELLO with a new boot id (the board restarted, or ended the session after a silence) fails the requests on the way (`link-lost`) and opens the session again (`reconnecting`, then `connected`).
- **A board known from an earlier visit** (`knownDevices()`, after a reload) is connectable only once the browser has seen it advertise: `WebBluetoothPlatform` answers the first failure by listening for one advertisement (`watchAdvertisements`; up to 10 s) and connecting again. A browser without it gives the first failure back; one without `getDevices()` lists no boards (`remembersBoards`). In Chrome and Edge both are behind flags: "Use the new permissions backend for Web Bluetooth" and "Experimental Web Platform features". A reload drops the link at once: with the dead-man armed the board enters its safe state when the session ends.
- **A board that restarted, on Windows:** Chromium's cached device often stays stale: its `connect()` fails, or it succeeds and the link drops as the service is read ("GATT Server is disconnected"). With the `refresh` option (the desktop app: a fresh scan for the remembered board) either failure is answered once by that scan and another connect. The loss of the link is listened for only once it is open, so a link that opened on the second try still reports its loss.
- **A drop is reopened at once**: the first attempt has no delay, then 0, 100, 250, 500, 1000, 2000 ms, the last repeating, until `disconnect()` (`reconnect: { delaysMs, maxAttempts, openTimeoutMs }`; `reconnect: false` turns it off). An attempt that neither works nor fails is dropped after `openTimeoutMs`; one that finishes late is closed.
- **Requests and the drop**: a request on the wire fails `link-lost` (it may have run); one given while the link is coming back waits for it (`waitForLinkMs`, default 5 s); with no link to wait for it fails `not-connected`.
- **A link that is down though nothing said so**: a write that fails recycles the link at once, and `stallAfter` (3) timeouts in a row do too. After the board applies a GATT change (SYS_BLE.MD "Run-time changes") Windows invalidates the handles and reports no disconnect. The old link is closed before the new one opens. A request that fails `send-failed` did not run (a frame runs only when its last packet arrives) and can be repeated. **`reopen()`** drops and opens again, to pick up a changed GATT table.
- **Writes**: a frame is written as packets back to back under one chain, so frames never interleave. A write goes without a response when the characteristic takes it. A packet is `packetBytes` long, its fragment byte included: 512 by default, the longest value ATT allows, so a frame of up to 511 B is one write and `CMD_FRAME_MAX` two.
- **Requests**: `maxInFlight` (4) at once; the board runs normal requests in order. An answer settles the request of its seq only when its key is that request's (else `unmatched`). A NOREPLY request (`noReply: true`) is done once written: no seq held, no timeout. An urgent one goes before every normal one queued and is written even when the window is full; the board runs it on its urgent task.
- **Streams**: every PUSH goes to `onStream` by its key: `{ key, name /* errors | events | logs | watches | remote; undefined for a key no stream has: an unknown-stream diagnostic */, src, seq /* the session's count of its class: a gap is frames lost; undefined on a raw characteristic */, urgent, data, at }`; `link.on('errors', (data) => …)` for one stream. A raw characteristic carries one whole frame per notification (`[hdr PUSH, key][data]`); the session's are reassembled, and a packet out of order drops the rest of its frame up to the next FIRST, as the firmware does. Nothing is interpreted: `decodeErrorRecord`, `decodeEvent`, `decodeWatchEntry` read the data. Diagnostics (`onDiagnostic`): `dropped`, `unknown-stream`, `not-a-frame`.

## The board's sessions

`Sessions` (`session/sessions.ts`, SYS_SESSION.MD "Remote"): the board's id and its sessions; a session's `role` is the published `session_flag` slave or master when its peer is a board, 0 for a client. PEERS is read by its descriptor (a count, then `sys_session_info_t` rows; `decodePeers`).

```ts
const sessions = new Sessions(link)
await sessions.boardId(); await sessions.setBoardId(1)           // its place in the project (NVS, from the next start); the descriptor's range
await sessions.peers()                                            // [{ provider, open, peerId?, safety, buildId, deadmanMs, role }]
```

## Watches

`Watches` (`session/watches.ts`, SYS_WATCH.MD): one `WATCH ADD` and the board reads and sends by itself. `watches.add({ head, read }, (entry) => …)` gives the watch an id of this client (1..255, shared by every `Watches` of one link: the board knows a watch by its owner and id, and an ADD of an id replaces it; an id is free again once its REMOVE is answered), sends ADD (the head by field id from `contract_watch.json`, then the read, a stored frame) and resolves with the function that removes it (`WATCH REMOVE`, sent only for a watch the board still has: a lost link took the session's). The entries come on the `watches` stream (the raw telemetry characteristic, every owner's): `[owner][id][status][data]`, `decodeWatchEntry`; the ones whose owner is `link.clientId` are this client's. The board ends a client's watches with its session, so `Watches` adds them again when the link comes back. `watches.follow(call, { trigger: 'every' | 'change', periodMs, silenceMs? }, (value) => …)` is `add` for a read of the catalog: periods in the board's ticks (`SYS_WATCH_TICK_MS`), each answer decoded as that command answers it, `undefined` when the read failed on the board.

## Records

`session/records.ts` (SYS_ACTIONS.MD): `storeRecord` (BEGIN, WRITE, COMMIT with the CRC; a WRITE carries what a body holds less a WRITE with no data), `recordInfo`, `readRecord` (from offset 0 to INFO's bytes, checked by the CRC), `listRecords` (LIST's lead is the total, asked from `start` until all came), `runRecord` (`keepGoing`: RUN_ALL), `removeRecord`; `crc32`, `frameImage`, `parseImage`. Commands and fields: `ACTIONS_COMMANDS`.

## The board's Bluetooth

`protocol/gatt.ts` (SYS_BLE.MD, PROVIDER_BLE.MD): a characteristic of the board's GATT table is a provider of the BLE type, so a client adds one as a **way** (`BleWay`: provider id, service, `chr` the one characteristic, written by the client and notified by the board, framed, name): `bleWayCall` is its PROVIDER CREATE by `provider_ble.json` (framed: `PROVIDER_BLE_FRAMED`; a way takes `CMD_SEND_NOWAIT`, so the BLE host never waits), `readBleWay` a stored CREATE back. `decodeBleTable` reads BLE LIST (services and characteristics with their published flags; the board's own service is `locked`). The app's GATT editor builds on them (`src/app/settings/gatt.ts`).

## The dead-man

Armed, the dead-man's time is the silence that ends this client's session, and the board enters its safe state (devices suspended: SYS_SYSTEM.MD) when that session ends: nothing came for that long, or the link dropped. Any frame feeds it, the link's own PINGs too. `link.setDeadman(ms)` sets the dead-man of this client's session at once (`SYS DEADMAN`; 0 is off) and keeps the link's PINGs to its third, as the Connections window does; `session/deadman.ts` keeps it armed from the app's side:

```ts
const deadman = new DeadMan(link, { timeoutMs: 1500 })        // 150..60000 ms; heartbeatMs: a third of the timeout
deadman.onEvent((e) => …)   // armed | restarted { uptimeMs } | lapsed { reason: 'link-lost' | 'silence', silentMs } | failed { step, error }
await deadman.start()        // the DEADMAN write now (or when the link opens), then fed until stop()
await deadman.stop()         // the DEADMAN write of 0; stop({ disarm: false }) leaves it to time out
```

- **Armed** with the DEADMAN write (urgent) when the link opens and after every reconnect: the setting lives in the board's RAM. The write sets the time of this session, with the safety flag (PEERS says both).
- **Fed** by a PING (urgent) only when nothing was written for `heartbeatMs`; the check runs every half heartbeat, so a heartbeat of 1/1.5 of the timeout or more is refused.
- **After a reconnect**, PING's uptime says whether the board restarted (`restarted`). If not, `lapsed` says the board has most likely entered its safe state: `link-lost` for a drop, `silence` when the last write before the drop is a timeout old. The board sends nothing when it enters it: this is the app's inference (`SYS SAFE` says the reason).
- Commands and fields are the descriptor's (`contract_sys.json`), named once in `DEADMAN_COMMANDS`; `deadman.test.ts` holds them against the catalog.
- **Leaving the safe state** is the caller's choice, never automatic (motors move again): `services.call('SYS', 'SAFE_LEAVE')`.
- A browser throttles timers of a background tab: a timeout of a few seconds survives the first throttling, not the second. The desktop app turns throttling off for its window (AGENTS.md "The desktop app").

## Errors for a user

An error record (ERRORS.MD "Error record": the board's clock when it was reported (`ErrorRecord.time`: UTC ms; before 2020 the clock was never set and it is a time since the start), the command (`source` `command`: its sender, seq, flags and request, a stored frame) or the board it is about (`source` `board`), then the chain from the route to the root cause) is read with what `errors.json` says about each tag: its message template, its fields, what an id field refers to (`id_kind`, `id_kind_by`), which kinds are numbered within another (`scope`, `of`), which nodes are context (severity NONE), and each node's severity, which the record does not carry. How errors.json gets them: templates/AGENTS.md "Errors".

```ts
const interpreter = new ErrorInterpreter({
  resolve: combineResolvers(programRefs(project, result), deviceRefs(new Map([[3, 'Left motor']]))),   // the app's names
})
const report = interpreter.read(data)              // or interpret(commandError.record)
report.message      // 'Pin 22 of Left motor is already set up as Input with Pullup'   (the root cause)
report.where        // ['In Devices', 'In Left motor', 'In IO pins: <command>']   (the context nodes, outermost first)
report.command      // { label: 'IO pins: <command> (Left motor)', refs }   (the request, when the record has it)
report.refs         // [{ kind: 'device', id: 3 }, { kind: 'pin', id: 22, scope: { kind: 'device', id: 3 } }, …]
report.chain        // every node: name, severity, context, message, fields ({ name, value, text, ref }), refs
interpreter.target(ref)   // { kind: 'pin', device: 3, pin: 22 } / { kind: 'block', … } / { kind: 'variable', variable: 'x' }
```

- **References** (`Ref = { kind, id, scope? }`, `refKey`: `device:3/pin:22`) are what an error is about. A scoped kind (a pin, a channel, a block pin, a device command) takes its instance from its `of` field, else from the one field of that kind in the same node, else from the nearest one before it in the chain or the request. The request of an addressed router (its descriptor has a contract id) is read as `[key][dir][id u8][op u16]`: the device router's as a device and its command.
- **Hints**: a node whose tag has a `hint` carries it filled in like the message (`NodeReport.hint`).
- **Names**: the app's resolver first (`programRefs`, `deviceRefs`), then the descriptors (device types, device commands, router keys, contracts, error tags), then the kind's alias and the number (`Pin 22`, `I2C address 0x3C` for a kind with `format: hex`). An enum field shows its member's alias; a field with a unit shows it. A tag errors.json does not have keeps its number (`known: false`).

`ErrorLog` keeps them for the user (dated by its record's clock: `setClock(unixMs, uptimeMs)`; a record read again is not counted twice):

```ts
const log = new ErrorLog(interpreter, { capacity: 200 })
log.attach(link)                                  // the errors stream; log.addCommandError(e) for a failed command
log.entries({ read: false, ref: 'device:3' })     // newest first; a device takes its pins' and channels' errors
log.markRead({ ref: 'device:3' }); log.clearRead() // or log.clear({ ref }) when the user fixed it
log.onChange((entries) => …)
```

- The board sends a command's error twice (the answer and the errors stream): the same record (sender, seq and chain) within `duplicateMs` (2 s) is one; the same chain again is the same entry, its `count` and `last` go up and it is unread again. Past `capacity` the oldest read entries go first.

## Where each fact comes from

| Fact | Source | Held against it by |
|---|---|---|
| The header, its bits, kinds, sources; the control ops; module numbers; stream keys | `enums.json` (`cmd_frame`, `cmd_type`, `cmd_kind_e`, `cmd_src`, `sys_session_control_e`, `module_id`) | `protocol.test.ts`: `cmd_hdr_t`'s fields and `CMD_HEAD` in `cmd.h`, the modules' `MODULE_ID_DECLARE`, `sys_session_hello_t`'s size |
| GATT UUIDs, fragment bits, table and provider flags, the watch tick | `enums.json` (`ble_runit_uuid`, `ble_frag`, `ble_svc_flag`, `ble_chr_flag`, `provider_ble_flag`, `cmd_send_flag`, `watch_tick`) | `protocol.test.ts` reads `runit_ids.h`, `sys_ble.h`, `provider_ble.h`, `sys_watch.h`; the framing tests use SYS_BLE.MD's examples |
| Error record | `ERRORS.MD`, `errors.json` | `protocol.test.ts`, `errors.test.ts`: records built from errors.json read back with names |
| Every command, its arguments and answer; provider types | `generated/services`, `contracts`, `devices`, `providers`; `device_contract_own`, `device_id`, `device_create_flag`, `device_remote_flag`, `provider_id`, `io_pin_none` in `enums.json` | `services.test.ts`: no problems in the bundle; every packed struct (`.h` and `.c`) against its layout; the requests' bytes; leads; providers and BLE ways |
| The events stream | `contract_event.json`, `contract_device.json` | `events.test.ts` |
| SESSION PEERS | `contract_session.json` (its lead and rows) | `sessions.test.ts`: the rows are `sys_session_info_t`'s 11 B, the fields `SESSION_COMMANDS` names |

## Tests

`npm test` from `runit-app/`. Both vitest configs set `pool: 'vmThreads'`: on this machine (Node 22.23, vitest 5.0.4) the default pool fails before a test runs. `tests/backend/fakes.ts` is a board behind a fake platform: it reassembles what is written on the session characteristic, opens the session itself (HELLO, SESSION PEERS) and hands every other request to the test as a decoded frame (`r.key`, `r.seq`, `r.flags`, `r.data`), answered with `answer(r, data)`; `push(key, data, { characteristic, seq, urgent, src })` is a stream frame. `records.ts` builds error records from errors.json. `link.test.ts` covers opening, requests, order, errors, timeouts, streams on every characteristic, the session (NO_SESSION, a restart, PINGs), reconnecting, the stall and the late open; `sessions.test.ts`, `follow.test.ts`, `deadman.test.ts`, `services.test.ts`, `protocol.test.ts`, `errors.test.ts`, `events.test.ts`, `webBluetooth.test.ts` the rest.

**The backend on a board:** `npm run test:hw` (`vitest.hardware.config.ts`, `tests/hardware/*.hw.test.ts`) runs this TypeScript backend against a devkit over the PC's Bluetooth. Node has no BLE, so `tests/hardware/bleakPlatform.ts` is a `BlePlatform` that moves packets through `bleak_bridge.py` (bleak, JSON lines on stdio; it opens what the `OpenSpec` names, the optional characteristics when the board has them); framing, requests and the connection logic stay the backend's. Environment: `HW_NAME` (default `runit`), `HW_ADDRESS`, `HW_PYTHON` (the Python with bleak, default the repository's `.venv`). One file at a time, in order:

| Suite | Covers |
|---|---|
| `a-link` | connect, MTU, 50 PINGs and 40 pipelined, an error answer and its copy on the errors stream, a long answer, a drop from the host reconnected |
| `b-services` | no catalog problems; every read of a service that takes no arguments answered and decoded; this client an open session in PEERS, SESSION ID, PROVIDER LIST and INFO, DATA_MATRIX LIST, BLE LIST; device 0 (the GPIO): INFO, OPS, a pin set as an output, LEVEL, TOGGLE read back, a pin it does not have, SUSPEND / RESUME, CREATE with an id in use |
| `c-errors` | records captured from the board read for a user; `ErrorLog`: answer and stream copy are one entry, repeats count up, read / clear by device, a burst of 20 |
| `d-deadman` | `DeadMan` armed (PEERS says its time and the safety flag), fed past its timeout and disarmed; STOP and SAFE_LEAVE; a session left silent ends, the safe state (reason DEADMAN), the session opened again; a restart under the dead-man |
| `h-watches` | a `follow` of PING every 200 ms and an ON_CHANGE watch with its silence |
| `i-streams` | the session as the board opened it (id, boot, build), a device suspended and resumed as events named by the DEVICE contract, an error on the errors stream with the record the answer carried |
| `j-router` | 9 pipelined TOGGLEs and one more (order kept); NOREPLY done once sent, its failure on the errors stream only; a body of the largest size read to its last byte, one byte more refused by the link (`too-long`) |

The suites change the board and put it back; no other BLE client may hold the board. A drop from the host is not a lost radio link on Windows, so the dead-man's link-lost path is held by the browser suite.

**In a browser:** `npm run test:browser` runs `WebBluetoothPlatform` itself in Chrome or Edge (`tests/browser/`: `main.ts` the suite in the page, `run.mjs` the runner, which answers the chooser through the DevTools protocol's `DeviceAccess` domain; `CHROME`, `CDP_PORT`). It covers connect, the session, the catalog, 90 PINGs, the errors stream, GPIO, events, the dead-man, the safe state, `reopen()`, a board restart, and reloading the tab with and without the dead-man armed. The runner starts the browser with `--enable-features=WebBluetoothNewPermissionsBackend` and `--enable-experimental-web-platform-features`.

## Not verified / not done

- The hardware and browser suites have not run on a board with this frame (the 5-byte header, the raw logs and telemetry characteristics): `npm test` holds the backend against the fake board and the C headers only.
- An abrupt loss of the radio (not a disconnect, not a restart) is untried. A silent session is found by the write and stall rules or by the OS supervision timeout.
- Web Bluetooth cannot read the MTU: a raw characteristic's item longer than one notification (MTU − 3) comes in pieces with no header on the later ones, and the link reads each notification as a frame (`not-a-frame` for the pieces). A PC and Chrome negotiate 517 or more.
- Only BLE. A UART or Wi-Fi platform would implement the same channel; `RunitLink` fragments for BLE only.
- The firmware's VM, project, OTA and flasher are not in the rework yet: the backend has no client for them.
- BLE LIST's layout is given in words (lists of lists): `decodeBleTable` reads it by hand, with the published flags; DATA_MATRIX LIST, WATCH LIST and SYS TASKS are still in words (their bytes in `Answer.data`).
