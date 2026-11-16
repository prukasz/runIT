# The backend: the link to a board (for agents)

`runit-app/src/backend/`: commands, the board's streams and a connection that keeps itself up, over BLE. No UI, no DOM beyond the Web Bluetooth platform. Written from the firmware (`runit-esp-rework/components/sys/sys_session/`, `sys/bus/sys_ble/`, `core/router/`) and `generated/`.

```ts
const platform = new WebBluetoothPlatform(bundledLinkDefinitions())
const link = new RunitLink(platform)
await link.connect(await platform.requestDevice())            // a user gesture; or platform.knownDevices() with no chooser
link.onStatus((s) => …)                                       // { state, reason?, attempt? }
link.onStream((f) => …)                                       // errors / events / logs / a module's channel: { stream, name, data, seq, at }
const services = new Services(link)
const { value } = await services.call('SYS', 'PING')          // { uptime_ms: 1234 }
await services.device(0, 'IO', 'TOGGLE', { pin: 4 })           // device 0 (the ESP32-S3's GPIO), contract IO
```

## Layout (dependencies point down)

| Module | What it is |
|---|---|
| `definitions.ts` | The only file that reads `generated/`: the root keys (`keys`: a router's key is its module's number, `module_id` in `enums.json`: `sys`, `session`, `watch`, `sys_device`, …), the direction byte (`dir`, `cmd_dir_e`), flags and status, the board's streams (`streams`: `logs`, `errors`, `events`, the ids of the stream tables `log_stream` and `event_stream`) and the session's channels (`channels`: `watches`, `remote`: a module's number with bit 7 set, `session_channel`), the session's frame byte, kinds, control ops and ids (`session_frame`, `sys_session_kind_e`, `sys_session_control_e`, `session_id`), the runIT GATT UUIDs and the fragment bits (`ble_runit_uuid`, `ble_frag`), the frame size `CMD_FRAME_MAX` and its header `CMD_HEAD` (`cmd_frame`: a request's keys and payload are at most their difference), the error record's header (`err_record_*`), every service, contract and device descriptor (`bundledServiceCatalog`, a glob: a new file is picked up), the firmware's onboard action ids (`onboard_action`), the errors (`errors.json`). A symbol the firmware stops publishing stops the load with a message that names it. |
| `catalog/` | `services.ts`: `ServiceCatalog`, every command of every router as the descriptors describe it (a device type also its pins and config presets) and what does not hold together in them (`problems`). `codec.ts`: a payload from field values and back, by the descriptor's layout. |
| `protocol/` | `frames.ts`: the session's frames (SYS_SESSION.MD "Frames"): the 3-byte header `[frame byte][seq u16]`, a request `[keys…][payload]`, an answer (status in the frame byte), a PUSH frame `[tag][payload]` (a tag from 0x80 is a module's channel), the control frames: HELLO (`sys_session_hello_t`: id, boot id, build id), PING, NO_SESSION. `peers.ts`: SESSION PEERS (`decodePeers`: each session's provider, open, peer id, safety, build, dead-man). `errors.ts`: the error record (source, command, chain of tags with their fields, named from `errors.json`). `events.ts`: `decodeEvent`, a frame of the events stream, a `sys_event_t` as it is, with its contract and its row of that contract's `events` table (`SUSPENDED` of a device, an over-current) and its payload. `watches.ts`: `WATCH ADD`'s call and a watch's entry. `services.ts`: the request of any catalog command and its answer decoded; `readDeviceFrame`, a device frame of a record back into its command, side and arguments. |
| `errors/` | `interpret.ts`: `ErrorInterpreter`, an error record read for a user (messages, the instances it is about as `Ref`s). `program.ts`: `programRefs` / `deviceRefs`, the app's names for those instances. |
| `transport/` | `types.ts`: what a BLE platform provides (`requestDevice`, `knownDevices`, `forget`, `open` → a channel to write packets to, packets and a loss notice back). `framing.ts`: a frame to packets and back, as the firmware does it (SYS_BLE.MD "Framing"). `webBluetooth.ts`: the Web Bluetooth platform. |
| `session/` | `commands.ts`: sequence numbers (u16), one answer by seq per request, timeouts, pipelining. `link.ts`: `RunitLink`, the connection and its session (HELLO with this client's own id, SESSION PEERS, PING). `services.ts`: `Services`, the catalog's commands over the link. `sessions.ts`: `Sessions`, the board's id and its sessions. `watches.ts`: `Watches`. `deadman.ts`: `DeadMan`, the board's dead-man armed and fed. `errorlog.ts`: `ErrorLog`, the board's errors kept for a user. `records.ts`: the records of the board (SYS_ACTIONS.MD): `storeRecord` (BEGIN, WRITE, COMMIT with the CRC), `recordInfo`, `readRecord`, `listRecords`, `runRecord`, `removeRecord`, and `crc32`, `frameImage`, `parseImage`; the ops are the ACTIONS service's, from the catalog. |

## Every command, from the descriptors

The backend names no service, contract, command or field: it reads them from the descriptors the firmware publishes (`templates/contract.schema.json`, `device.schema.json`). How an entry is addressed follows from what its descriptor has:

| Entry | Has | Request |
|---|---|---|
| service | `router_key` (`generated/services`) | `[router key][dir][op][args]` |
| service addressed by an id | `router_key` and `contract_id` (PROVIDER) | `[router key][dir][id u8][(contract id << 8) \| op u16][args]`; the id is the provider's, `id` of the call (none left out: LIST) |
| device contract | `contract_id`, `scope: device` (`generated/contracts`) | `[sys_device key][dir][device id u8][(contract id << 8) \| op u16][args]` (SYS_DEVICE.MD "IDs, ops, contracts") |
| device type | `type_id` (`generated/devices`) | its own ops in contract `SYS_DEVICE_CONTRACT_OWN` (`device_contract_own`); CREATE (the device contract `SYS_DEVICE_CONTRACT_DEVICE`, a write) carries `[type u16][flags u8][config]` (flags: the board's own device) |

A contract id and a router key are module numbers (`module_id`); the device contract is the one that is both (`catalog.lifecycleContractId`).

**A command has a read side, a write side or both** (`CommandDef.read`, `.write`: each its own `args` and `returns`), and every request says which with the byte after the root key (`dir`: `cmd_dir_e`). A command with both is a mirror: its read takes the key (a pin, a channel; nothing for a module's setting) and answers what its write takes. A write answers no data unless its side has `returns`. A call of a command with one side runs it; one with both is told which (`side`, else it throws), and `sidesOf`, `sideOf`, `isMirror` say what a command has.

```ts
const services = new Services(link)                                              // the bundled catalog by default
const { value } = await services.call('SYS', 'PING')                             // { uptime_ms: 1234 }: PING has a read only
await services.call('SYS', 'DEADMAN', { timeout_ms: 500 }, { side: 'write' })
await services.call('SYS', 'DEADMAN', undefined, { side: 'read' })               // { timeout_ms: 500 }: the mirror
await services.call('PROVIDER', 'INFO', undefined, { id: 3 })                    // a service addressed by an id
await services.device(0, 'IO', 'LEVEL', { pin: 4, level: 'High' }, { side: 'write' })   // a choice by its label, an enum by its member name
await services.device(0, 'IO', 'LEVEL', { pin: 4 }, { side: 'read' })           // { pin: 4, level: 1 }: the key, then the value
await services.device(9, 'dc_motor', 'speed', { speed_x10: 90 }, { side: 'write' })   // a device type's own op (90 %: a field's `scale` 10 makes it 900 on the wire)
await services.create(20, 'dc_motor', { bridge: 5, channel: 0, … })              // the fields of device_dc_motor.json's config; a pin is { device, pin }
await services.createRemote(20, 'esp32s3_gpio', 2, 0)                            // device 20 here is an alias of device 0 of board 2 (DEVICE CREATE_REMOTE, SYS_REMOTE.MD); 'wired' as the last argument for a cable
readDeviceFrame(defs, catalog, frame, (id) => typeOfDevice(id))                  // a frame read back: { device, entry, command, side, args } or { …, create: { type, system, config } }
```

- **Layout**: a field sits at its `offset` (or after the one before), is `size` bytes (or its `raw_type`'s width); a number field of several widths is an array (`u32` of 16 B is a list of 4); a `bytes` / `string` field without a size runs to the end and is last; a `string` ends in one NUL (the router checks it). Little endian.
- **A bitmask** field (`type: bitmask`, the descriptor's `bits` name the ones offered) takes its number: the bits set, as an integer; the form offers each named bit as a chip.
- **Values** are given by field id. A missing field takes its `default`, an optional one zero; else the call throws `PayloadError`, as it does for a field id that is not there, a value outside the raw type or the descriptor's `min` / `max`, and an unknown choice label or enum member. A pin reference (`pin_ref`, a `sys_io_pin_t` of 2 B: device u8, pin u8, or a list of them) is `{ device, pin }`, `null` for an unused pin (`SYS_IO_PIN_NONE`, `io_pin_none` in `enums.json`); an optional pin left out is unused, never device 0 / pin 0. A command marked `urgent` is sent urgent. `CommandDef.recordId` is the settings record the frame of its write is kept in (`record`, resolved through `settings_record` in enums.json); `CommandDef.tags` the descriptor's (`app`: a command the app sets itself, the clock). `WireField.indexEnum` names the enum whose members are an array field's elements. A side whose arguments are rows (`array`: the power response matrix) takes a list of structs.
- **Answers** are decoded by the side's `returns`: a struct by field id (booleans as booleans, pins as `{ device, pin }` or `null`), a value, or a list of either. A value that only leads the answer (a count, the rest described in words: `PROVIDER LIST`, `DATA_MATRIX LIST`, `BLE LIST`) is read from the front; `SESSION PEERS` has `decodePeers`; `Answer.data` keeps the bytes.
- **Problems**: overlapping fields, a field that does not fit its raw type, a router key or contract id that is no published module, an op twice, a command with neither side, a device naming a contract nobody describes, a pin reference that is not bytes of a multiple of 2, an unpublished `io_pin_none`, `device_id`, `device_remote_flag`, `device_contract_own` or `cmd_dir_e`. They go to `catalog.problems`; the rest of the catalog stays usable. `services.test.ts` holds the bundle at none, and holds every layout of a packed C struct against the struct in `runit-esp-rework/components/**/*.h` (its size, each field inside the member of its name).

## The connection

- **Opening is one platform call**: connect, the runIT service alone (no walk over every service), its characteristics in one call, and a subscription to the two notify characteristics the link uses (the session's frames `0xFFE1`, the log lines `0xFFE9`). The browser runs one GATT operation at a time: they go through one chain.
- **Then the session** (SYS_SESSION.MD "A session's life"): each side says its own id in its HELLO. The link sends its hello with the app's id (the first client id, `SYS_SESSION_ID_CLIENT_FIRST`, 0xF0; `LinkOptions.clientId` for another client, one of `SYS_SESSION_ID_CLIENT_FIRST..LAST`: no two open sessions of a board have the same) and a boot id of its own (random per link), again every 0.5 s until answered (the board may not have heard the first: the one it sent at connect came before the link subscribed); the board's HELLO gives its id, boot id and build (`link.session`: `clientId`, `boardId`, `bootId`, `buildId`); then SESSION PEERS gives this session's dead-man (`deadmanMs`, 0: none). Only then is it `connected`. **Nothing is subscribed**: the board's setup routes its errors and events to the session (`app/runit`, RUNIT.MD) and the log lines to their own characteristic. With the dead-man on, something goes to the board at least every third of its time (a PING when nothing else does). A NO_SESSION or a HELLO with a new `boot_id` (the board restarted, or ended the session after a silence) fails the commands on the way (`link-lost`) and opens the session again (`reconnecting`, then `connected`). The board says NO_SESSION to the first frame after its silence, so a link that sends nothing learns it with the next command.
- **A board known from an earlier visit** (`knownDevices()`, after a reload of the tab) is connectable only once the browser has seen it advertise: before that `connect()` fails with "Bluetooth Device is no longer in range". `WebBluetoothPlatform` answers that first failure by listening for one advertisement (`watchAdvertisements`, in a browser that has it; up to 10 s) and connecting again, so a reloaded page opens the board with no chooser. A browser without `watchAdvertisements` gives the first failure back with that said; one without `getDevices()` lists no boards (`remembersBoards` is false). In Chrome and Edge both are behind flags: "Use the new permissions backend for Web Bluetooth" and "Experimental Web Platform features". A reload drops the link at once: with the dead-man armed, the board enters its safe state when the session ends, and otherwise carries on.
- **A drop is reopened at once**: the first attempt has no delay (the board advertises again as soon as it drops), then 0, 100, 250, 500, 1000, 2000 ms, the last repeating, until `disconnect()` (`reconnect: { delaysMs, maxAttempts, openTimeoutMs }`; `reconnect: false` turns it off). An attempt that neither works nor fails is dropped after `openTimeoutMs`; one that finishes late is closed.
- **Commands and the drop**: a command on the wire fails `link-lost` (it may have run: only the caller knows if it can be repeated); one given while the link is coming back waits for it (`waitForLinkMs`, default 5 s); with no link to wait for it fails `not-connected`.
- **A link that is down though nothing said so**: a write that fails recycles the link at once, and `stallAfter` (3) timeouts in a row do too. After the board applies a GATT change (SYS_BLE.MD "Runtime changes") Windows invalidates the handles and reports no disconnect: every write fails with `The object has been closed`. The old link is closed before the new one opens. A command that fails `send-failed` did not run (a frame runs only when its last packet arrives) and can be repeated.
- **`reopen()`** drops and opens again, to pick up a changed GATT table (the new service is visible after it).
- **Writes**: one request is written as packets back to back under one chain, so frames never interleave (the firmware reassembles with one buffer). A write goes without a response when the characteristic takes it, about ten times faster than an acknowledged one. A packet is `packetBytes` long, header included: 512 by default, the longest value ATT allows (513 B is refused with `Invalid Attribute Value Length`), so a frame of up to 511 B is one write and the 512 B maximum (`CMD_FRAME_MAX`) two. Web Bluetooth does not show the MTU; the board offers 527 and a central that takes it (Windows does) needs no splitting.
- **Pipelining**: `maxInFlight` (4) commands at once. The board runs normal commands in order, so this saves a round trip per command and reorders nothing. A NOREPLY request (`noReply: true`) is done once written: the board never answers it (a failure goes to the errors stream only), so it holds no seq and no timeout. An urgent request (`urgent: true`) goes before every normal one still queued and is written even when the window is full; on the board it runs on the urgent task.
- **Streams**: every PUSH frame goes to `onStream` by its first byte (without it), raw, with the time it arrived (`at`): `link.onStream((f) => f.name /* errors | events | logs: the board's; watches | remote: a module's channel (0x80 | its module number); undefined for a tag nobody publishes */, f.stream, f.data, f.at, f.seq /* the session's count of its pushes of that class: a gap is frames lost; undefined for logs */)`; a tag neither published nor a channel is an `unknown-stream` diagnostic; `link.on('errors', (data) => …)` for one stream. Nothing is interpreted: errors and logs are bytes (`decodeErrorRecord`, `decodeEvent`). The log lines come as they are on `0xFFE9`, with no header. A packet out of order drops the rest of its frame up to the next FIRST, as the firmware does; `onDiagnostic` says so.

## The board's sessions

`Sessions` (`session/sessions.ts`, SYS_SESSION.MD "Remote"): the board's id and its sessions. A session is made and ended by the data matrix's roles (`DATA_MATRIX ROLE`, `CLEAR_ROLE`, DATA_MATRIX.MD), which the catalog's commands reach.

```ts
const sessions = new Sessions(link)
await sessions.boardId(); await sessions.setBoardId(1)           // its place in the project (NVS, from the next start)
await sessions.peers()                                            // [{ provider, open, peerId?, safety, buildId, deadmanMs }]
```

## Watches

`Watches` (`session/watches.ts`, SYS_WATCH.MD): one `WATCH ADD` and the board reads and sends by itself, so a view that follows a value asks for nothing more. `watches.add({ head, read }, (entry) => …)` gives the watch an id of this client (ids are shared by every `Watches` of one link: the board knows a watch by its owner and id, and an ADD of an id replaces that watch; an id is free again once its REMOVE is answered), sends ADD (`watchAddCall`, `protocol/watches.ts`: the head by field id from `contract_watch.json`, then the read, its last field: `requestFrame` of a read call) and resolves with the function that removes it (`WATCH REMOVE`, sent only for a watch the board still has: one whose link was lost since went with the board's session, and a REMOVE of it would be `ERR_WATCH_NOT_FOUND` there). The entries come on the `watches` channel, which needs no subscription (they are sent to the one who added the watch): `[id][status][data]`, `decodeWatchEntry`; `ok` says `data` is the read's answer, else it is an error record. The board ends a client's watches with its session, so `Watches` adds them again when the link comes back. `watches.follow(call, { trigger: 'every' | 'change', periodMs, silenceMs? }, (value) => …)` is `add` for a read of the catalog (`serviceCall` / `deviceCall`, read side): the head is made from the spec and each answer reaches the handler decoded as that command answers it (`decodeAnswer`), `undefined` when the read failed on the board.

## The dead-man

Armed, the dead-man's time is the silence that ends this client's session, and the board enters its safe state (devices suspended: SYS_SYSTEM.MD) when that session ends: nothing came for that long, or the link dropped. Any frame feeds it, the link's own PINGs too (for a dead-man the link saw at open). `session/deadman.ts` keeps it:

```ts
const deadman = new DeadMan(link, { timeoutMs: 1500 })        // 150..60000 ms; heartbeatMs: a third of the timeout
deadman.onEvent((e) => …)   // armed | restarted { uptimeMs } | lapsed { reason: 'link-lost' | 'silence', silentMs } | failed { step, error }
await deadman.start()        // the DEADMAN write now (or when the link opens), then fed until stop()
await deadman.stop()         // the DEADMAN write of 0; stop({ disarm: false }) leaves it to time out
```

- **Armed** with the DEADMAN write (sent urgent) when the link opens and after every reconnect: the setting lives in the board's RAM and a restart forgets it. The write sets the time of this session, with the safety flag (`SESSION PEERS` says both).
- **Fed** by a PING (urgent) only when nothing was written for `heartbeatMs`; the check runs every half heartbeat, so a heartbeat of 1/1.5 of the timeout or more is refused.
- **After a reconnect**, PING's uptime says whether the board restarted (`restarted`: its devices and the dead-man are as at boot). If not, `lapsed` says the board has most likely entered its safe state: `link-lost` for a drop, `silence` when the last write before the drop is a timeout old. The board sends nothing when it enters its safe state: this is the app's inference (`SYS SAFE` says the reason and since when).
- Commands and fields are the descriptor's (`contract_sys.json`), named once in `DEADMAN_COMMANDS`; `deadman.test.ts` holds them against the catalog.
- **Leaving the safe state** is the caller's choice, never automatic (motors move again): `services.call('SYS', 'SAFE_LEAVE')`.
- A browser throttles timers of a background tab (to once a second, and to once a minute after a while in Chrome): a timeout of a few seconds survives the first, not the second. The desktop app turns throttling off for its window (AGENTS.md "The desktop app").

## Errors for a user

An error record (ERRORS.MD "Error record": the board's clock when it was reported (`ErrorRecord.time`: UTC ms; before 2020 the clock was never set and it is a time since the start), the command (`source` `command`) or the board it is about (`source` `board`, `ErrorRecord.board`: an error about another board as a whole), then the chain from the route to the root cause) is read with what `errors.json` says about each tag: its message template, its fields, what an id field refers to (`id_kind`, `id_kind_by`), which kinds are numbered within another (`scope`, `of`), which nodes are context (severity NONE), and each node's severity, which the record does not carry. How errors.json gets them: templates/AGENTS.md "Errors".

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
interpreter.target(ref)   // { kind: 'pin', device: 3, pin: 22 } / { kind: 'block', block: 'e', pin: { kind: 'input', index: 1 } } / { kind: 'variable', variable: 'x' }
```

- **References** (`Ref = { kind, id, scope? }`, `refKey`: `device:3/pin:22`) are what an error is about, so the app can show it on the device, the pin, the variable or the block. A scoped kind (a pin, a channel, a block pin, a device command) takes its instance from its `of` field, else from the one field of that kind in the same node, else from the nearest one before it in the chain or the request (`ERR_DEV_UNKNOWN_OP` under `ERR_DEV_ID` is a command of that device).
- **Hints**: a node whose tag has a `hint` in errors.json (what the user can do about it, `errors_<module>.json`) carries it filled in like the message (`NodeReport.hint`); the app shows the root cause's under the message. The C comment of the map stays the node's `description`, for developers.
- **Names**: the app's resolver first (`programRefs`: objects to variables or to the block output that owns them, accessors to the block input or enable that reads through them, blocks and their pins to block instances; `deviceRefs`: the project's devices), then the descriptors (device types, device commands, router keys, contracts, error tags), then the kind's alias and the number (`Pin 22`, `I2C address 0x3C` for a kind with `format: hex`). An enum field shows its member's alias; a field with a unit shows it.
- A tag errors.json does not have keeps its number (`known: false`).

`ErrorLog` keeps them for the user (an entry is dated by its record's clock: `setClock(unixMs, uptimeMs)` tells it the board's clock so a time since the start becomes UTC; a record read again, as the board's history is each time the link comes up, is not counted twice):

```ts
const log = new ErrorLog(interpreter, { capacity: 200 })
log.attach(link)                                  // the errors stream; log.addCommandError(e) for a failed command
log.entries({ read: false, ref: 'device:3' })     // newest first; a device takes its pins' and channels' errors
log.markRead({ ref: 'device:3' }); log.clearRead() // or log.clear({ ref }) when the user fixed it
log.onChange((entries) => …)
```

- The board sends a command's error twice (the answer and the errors stream) and never suppresses repeats: the same record (sender, seq and chain) within `duplicateMs` (2 s) is one; the same chain again is the same entry, its `count` and `last` go up and it is unread again. Past `capacity` the oldest read entries go first.
- What the backend knows by itself: the record's layout (from `enums.json`), which descriptors name which kinds (`device_type`, `device_op`, `router_key`, `contract`, `error_tag` in `ErrorInterpreter`) and how the program's kinds map to the project (`errors/program.ts`: needs the engine, which the firmware's VM will serve again). Messages, kinds, scopes and field meanings are errors.json's.

## Where each fact comes from

| Fact | Source | Held against it by |
|---|---|---|
| Root keys, the session's ids, frame byte, kinds, control ops, channels, the streams | `enums.json` (`module_id`, `session_*`, `sys_session_*`, `log_stream`, `event_stream`) | `protocol.test.ts` reads the routers' `MODULE_ID_DECLARE` |
| GATT UUIDs, fragment bits | `enums.json` (`ble_runit_uuid`, marked in `runit_ids.h`; `ble_frag`, in `sys_ble.h`) | `protocol.test.ts` reads both headers; the framing tests use the examples of SYS_BLE.MD |
| Frame limit and header | `enums.json` (`cmd_frame`: `CMD_FRAME_MAX`, `CMD_HEAD`) | `protocol.test.ts` reads `cmd.h` |
| Error record | `ERRORS.MD`, `errors.json` | `protocol.test.ts` decodes a record built from errors.json |
| Every other command, its arguments and answer | `generated/services`, `generated/contracts`, `generated/devices`; `device_contract_own`, `device_id`, `device_remote_flag`, `io_pin_none` in `enums.json` | `services.test.ts`: no problems in the bundle; every packed struct's layout against its C typedef; the device frame's bytes; `sys_io_pin_t` is the 2 B of a pin reference |
| The events stream: `sys_event_t`, the device contract's events | `contract_event.json`, `contract_device.json` | `events.test.ts`: `sys_event_t` is 20 B, an event read with its contract and name, the DEVICE events equal `SYS_DEVICE_EVENTS` |
| Error messages, what an error field refers to | `errors.json` (`message`, `id_kind`, `id_kind_by`, `of`, `context`, `id_kinds`), from the maps and `errors_<module>.json` | `errors.test.ts`: every placeholder is a field, every kind is described; records built from errors.json read back with names |
| The session's hello, SESSION PEERS | `SYS_SESSION.MD`, `sys_session.h`, `sys_session_router.h` | `protocol.test.ts` (the hello's 10 B), `sessions.test.ts` (PEERS byte by byte, the link's session) |

## Tests

`npm test` from `runit-app/`. Both vitest configs set `pool: 'vmThreads'`: on this machine (Node 22.23, vitest 5.0.4) the default pool fails before a test runs (`Cannot read properties of undefined (reading 'config')`). `tests/backend/fakes.ts` is a board behind a fake platform (reassembles what is written, opens the session itself: HELLO, SESSION PEERS; answers on notify, drops the link, fails or hangs an open); `records.ts` builds error records from errors.json. `link.test.ts` covers requests, order, parts, errors, timeouts, streams, reconnecting, the stall and the late open; `sessions.test.ts` the session and the PEERS layout.

**The backend on a board:** `npm run test:hw` (`vitest.hardware.config.ts`, `tests/hardware/*.hw.test.ts`) runs this TypeScript backend itself, the real `RunitLink`, `Services`, `DeadMan`, `ErrorLog` and `Watches`, against a devkit over the PC's Bluetooth. Node has no BLE, so `tests/hardware/bleakPlatform.ts` is a `BlePlatform` that moves packets through `bleak_bridge.py` (bleak, JSON lines on stdio; it opens the device its scan found, not an address); framing, commands and the connection logic stay the backend's. Environment: `HW_NAME` (advertised name, default `runit`), `HW_ADDRESS`, `HW_PYTHON` (the Python with bleak: a path to its executable, default the repository's `.venv`). One file at a time, in order:

| Suite | Covers |
|---|---|
| `a-link` | connect, MTU, 50 PINGs and 40 pipelined, an error answer and its copy on the errors stream, a long answer, a drop from the host reconnected |
| `b-services` | no catalog problems; every read of a service that takes no arguments answered and decoded; this client an open session in PEERS, SESSION ID, PROVIDER LIST and INFO, DATA_MATRIX LIST, BLE LIST; device 0 (the GPIO): INFO, OPS, a pin set as an output, LEVEL, TOGGLE read back, a pin it does not have, SUSPEND / RESUME, CREATE with an id in use |
| `c-errors` | records captured from the board read for a user (the app's device names, the command); `ErrorLog`: answer and stream copy are one entry, repeats count up, read / clear by device, a burst of 20 |
| `d-deadman` | `DeadMan` armed (PEERS says its time and the safety flag), fed past its timeout and disarmed; STOP and SAFE_LEAVE; a session left silent ends, the board enters the safe state (reason DEADMAN) and the link opens the session again; a restart under the dead-man |
| `h-watches` | a `follow` of PING every 200 ms (entries with no request after the first, none after the REMOVE) and an ON_CHANGE watch with its silence |
| `i-streams` | the session as the board opened it (id, boot, build), a device suspended and resumed as events named by the DEVICE contract, an error on the errors stream with the record the answer carried |
| `j-router` | 9 pipelined TOGGLEs and one more (order kept); NOREPLY done once sent, its failure on the errors stream only, naming this client and the flag; a request of the largest size read to its last byte, one byte more refused by the link (`too-long`) |

The suites change the board and put it back: pin 4 is set up and toggled, device 0 is suspended and resumed, the dead-man is left disarmed, and the board restarts once. No other BLE client may hold the board. A drop from the host (`dropFromHost`) is not a lost radio link to the board on Windows: it reuses the closed GATT connection, so the dead-man's link-lost path is held by the browser suite.

**In a browser:** `npm run test:browser` runs `WebBluetoothPlatform` itself, the platform the app ships, in Chrome or Edge (`tests/browser/`: `main.ts` is the suite in the page, `run.mjs` the runner). Vite serves the page; the runner drives the browser over the DevTools protocol with Node's own WebSocket and answers the Web Bluetooth chooser through the `DeviceAccess` domain (it takes the board named `HW_NAME`, default `runit`, once the scan shows it: nobody clicks). `CHROME` picks the browser (default Chrome, else Edge; `CDP_PORT` if 9333 is taken). It covers connect, the session, the catalog, 90 PINGs, the errors stream, GPIO, events, the dead-man, the safe state, `reopen()` and a board restart with the link coming back by itself; then reloading the tab: with the dead-man armed (the new page connects with no chooser, the uptime goes on, the board is in its safe state, `SAFE_LEAVE` resumes it), with nothing armed (no safe state), and three reloads in a row. The runner starts the browser with `--enable-features=WebBluetoothNewPermissionsBackend` and `--enable-experimental-web-platform-features` (`getDevices()` and `watchAdvertisements`). Needs no other BLE client on the board; the board restarts once.

## Not verified / not done

- `WebBluetoothPlatform` has run in Chrome on Windows (`npm run test:browser`); Edge and Android are untried. The TypeScript link has run against a devkit through bleak on Windows (`npm run test:hw`).
- An abrupt loss of the radio (not a disconnect, not a restart) is untried. A silent session is found by the write and stall rules or by the OS supervision timeout.
- Web Bluetooth cannot read the MTU, the RSSI or the connection parameters; a native platform can implement `BlePlatform` and give them.
- Only BLE. A UART or Wi-Fi platform would implement the same channel; `RunitLink` fragments for BLE only.
- The firmware's VM, project, OTA and flasher are not in the rework yet: the backend has no client for them (the engine, `src/app/` and the tests of both are written against them and wait for them). The log lines are held by the link's unit test; no command of the devkit made the firmware send one over `0xFFE9` in the hardware suite.
- The messages of `ErrorInterpreter` are English. Answers whose layout a descriptor gives only in words (`BLE LIST`, `PROVIDER LIST`, `DATA_MATRIX LIST`, the logs stream) are not decoded past their leading value; SESSION PEERS is decoded by hand from the firmware's struct (`protocol/peers.ts`), so a change there is not seen until that file follows it.
