# The backend: the link to a board (for agents)

`runit-app/src/backend/`: commands, streams and a connection that keeps itself up, over BLE. No UI, no DOM beyond the Web Bluetooth platform. Written from the firmware (`runit-esp/components/sys/sys_ble/`, `runit-esp/components/core/router/`) and `generated/`.

```ts
const link = new RunitLink(new WebBluetoothPlatform(bundledLinkDefinitions()))
await link.connect(await platform.requestDevice())            // a user gesture; or platform.knownDevices() with no chooser
link.onStatus((s) => …)                                       // { state, reason?, attempt? }
link.onStream((f) => …)                                       // telemetry / logs / errors / events: { stream, name, data }
const out = await link.request({ keys: [defs.keys.sys, defs.dir.read, 0x01] }) // PING; keys = root key, the side (dir), then the op
await loadProgram(link, engine.compile(project).frames, { onProgress: (done, total) => … })   // in order, each answered; again from OPEN if the link fails
```

## Layout (dependencies point down)

| Module | What it is |
|---|---|
| `definitions.ts` | The only file that reads `generated/` (the firmware's own actions: `onboardActions`, `RUNIT_ACTION_ID_*`): router keys, flags, status, stream bytes, the runIT GATT UUIDs and the fragment bits (`ble_runit_uuid`, `ble_frag` in `enums.json`), `ROUTER_FRAME_MAX` (`limits.json`), the error tags (`errors.json`); the direction byte (`dir`, `cmd_dir_e`); the ops of the `actions` and `project` routers by command id (`ops`) with the side each is sent with (`opDirs`: its write, else its read) and the project's boot option (`projectBoot`: none / load / run, `sys_project_boot_e`); every service, contract and device descriptor (`bundledServiceCatalog`, a glob: a new file is picked up); the board (`bundledBoard`, `board.json`, or `board_devkit.json` when the app was started for a devkit: `appBoardKind`, `VITE_RUNIT_BOARD`; its devices, the pins each uses, I2C buses, the pins set up at boot and the reserved ones, and `power`: the input and each regulator rail by the power monitor channel that measures it) and the firmware's onboard action IDs (`onboardActionIds`, the `RUNIT_ACTION_ID_*` limits). A symbol the firmware stops publishing stops the load with a message that names it. |
| `catalog/` | `services.ts`: `ServiceCatalog`, every command of every router as the descriptors describe it (a device type also its pins, `provides.pins`, and its config presets), and what does not hold together in them (`problems`). `codec.ts`: a payload from field values and back, by the descriptor's layout. |
| `protocol/` | `frames.ts`: request `[seq][flags][keys…][payload]`, response `[stream][seq][status][data]` (ROUTER.MD). `errors.ts`: the error record (source, command, chain of tags with their fields, named from `errors.json`). `events.ts`: `decodeEvent`, a frame of the events stream (`[event key][NOTIFY][sys_event_t]`, SYS_EVENT.MD) with its contract and its row of that contract's `events` table (`SUSPENDED` of a device, an over-current). `services.ts`: the request of any catalog command (one side of it) and its answer decoded; `readDeviceFrame`, a device frame of a record back into its command, side and arguments (a CREATE into the type and its config). |
| `errors/` | `interpret.ts`: `ErrorInterpreter`, an error record read for a user (messages, the instances it is about as `Ref`s). `program.ts`: `programRefs` / `deviceRefs`, the app's names for those instances. |
| `transport/` | `types.ts`: what a BLE platform provides (`requestDevice`, `knownDevices`, `forget` (drops the permission for a board: only the chooser offers it again), `open` → a channel to write packets to, packets and a loss notice back). `framing.ts`: a frame to packets and back, as the firmware does it (SYS_BLE.MD "Framing"). `webBluetooth.ts`: the Web Bluetooth platform. |
| `session/` | `commands.ts`: sequence bytes, answers by seq, timeouts, pipelining, answers in parts. `link.ts`: `RunitLink`, the connection. `send.ts`: `sendInOrder` / `sendProgram`, a list of requests sent in order; `loadProgram`, a program loaded with retries and a RESET when it gives up. `services.ts`: `Services`, the catalog's commands over the link. `deadman.ts`: `DeadMan`, the board's dead-man armed and fed. `safestate.ts`: `leaveSafeState`, every device resumed (`SYS SAFE_LEAVE`, which also clears the board's note of the safe state; a firmware without it gets the RESUME sweep) and the program started again. `errorlog.ts`: `ErrorLog`, the board's errors kept for a user. `project.ts`: stored records, the project stream, reading a project back. |

## Every command, from the descriptors

The backend names no service, contract, command or field: it reads them from the descriptors the firmware publishes (`templates/contract.schema.json`, `device.schema.json`). How an entry is addressed follows from what its descriptor has:

| Entry | Has | Request |
|---|---|---|
| service | `router_key` (`generated/services`) | `[router key][dir][op][args]` |
| device contract | `contract_id`, no router key (`generated/contracts`) | `[device key][dir][device id u16][(contract id << 8) \| op u16][args]` (SYS_DEVICE.MD "Ops") |
| device type | `type_id` (`generated/devices`) | its own ops in contract `SYS_DEVICE_CONTRACT_OWN`; CREATE (device contract `SYS_DEVICE_CONTRACT_DEVICE`, a write) carries `[type u16][config]` |

**A command has a read side, a write side or both** (`CommandDef.read`, `.write`: each its own `args` and `returns`), and every request says which with the byte after the root key (`dir`: `cmd_dir_e` of `cmd.h`, in `enums.json`; `LinkDefinitions.dir`, `catalog.dirs`). A command with both is a mirror: its read takes the key (a pin, a channel; nothing for a module's setting) and answers what its write takes. A write answers no data unless its side has `returns`. A call of a command with one side runs it; one with both is told which (`side`, else it throws), and `sidesOf`, `sideOf`, `isMirror` say what a command has.

```ts
const services = new Services(link)                                              // the bundled catalog by default
const { value } = await services.call('SYS', 'PING')                             // { uptime_ms: 1234 }: PING has a read only
await services.call('SYS', 'DEADMAN', { timeout_ms: 500, on_link_lost: true }, { side: 'write' })
await services.call('SYS', 'DEADMAN', undefined, { side: 'read' })               // { timeout_ms: 500, on_link_lost: true }: the mirror
await services.device(3, 'IO', 'LEVEL', { pin: 22, level: 'High' }, { side: 'write' })   // a choice by its label, an enum by its member name
await services.device(3, 'IO', 'LEVEL', { pin: 22 }, { side: 'read' })          // { pin: 22, level: 1 }: the key, then the value
await services.device(9, 'servo', 'target', { value_x10: 90 })                   // a device type's own op (90°: a field's `scale` 10 makes it 900 on the wire)
await services.create(20, 'servo', { pwm_pin: { device: 3, pin: 0 }, … })        // the fields of device_servo.json's config; a pin is { device, pin }
storeRecord(link, 10, [requestFrame(deviceCall(defs, catalog, 3, 'IO', 'TOGGLE', { pin: 22 }).request)])   // the same request in a record
readDeviceFrame(defs, catalog, frame, (id) => typeOfDevice(id))                  // a frame read back: { device, entry, command, side, args } or { …, create: { type, config } }
```

- **Layout**: a field sits at its `offset` (or after the one before), is `size` bytes (or its `raw_type`'s width); a number field of several widths is an array (`u32` of 16 B is a list of 4); a `bytes` / `string` field without a size runs to the end and is last; a `string` ends in one NUL (`cmd_name_arg`). Little endian.
- **A bitmask** field (`type: bitmask`, the descriptor's `bits` name the ones offered) takes its number: the bits set, as an integer; the form offers each named bit as a chip.
- **Values** are given by field id. A missing field takes its `default`, an optional one zero; else the call throws `PayloadError`, as it does for a field id that is not there, a value outside the raw type or the descriptor's `min` / `max`, and an unknown choice label or enum member. A pin reference (`pin_ref`, a `sys_io_pin_t`, or a list of them) is `{ device, pin }`, `null` for an unused pin (`SYS_IO_PIN_NONE`, `io_pin_none` in `enums.json`); an optional pin left out is unused, never device 0 / pin 0. A command marked `urgent` is sent urgent. Its write changes the pin, channel or device it is given; a UI does not offer it a pin another device manages. `CommandDef.recordId` is the settings record the frame of its write is kept in (`record`, resolved through `settings_record` in enums.json; also `CatalogEntry.recordId` for a service kept as one record); `CommandDef.tags` the descriptor's (`app`: a command the app sets itself, the clock). `WireField.indexEnum` names the enum whose members are an array field's elements. A side whose arguments are rows (`array`: the power response matrix) takes a list of structs.
- **Answers** are decoded by the side's `returns`: a struct by field id (booleans as booleans, pins as `{ device, pin }` or `null`), a value, or a list of either. A value that only leads the answer (a count, the rest described in words: `CONNECTOR INFO`, `BLE LIST`) is read from the front; `Answer.data` keeps the bytes.
- **Problems**: overlapping fields, a field that does not fit its raw type, a router key or contract id the firmware does not publish (`router_key`, `device_contract` in `enums.json`), an op twice, a command with neither side, a device naming a contract nobody describes, a pin reference that is not bytes of a multiple of 3, an unpublished `io_pin_none`, `device_id` or `cmd_dir_e`. They go to `catalog.problems`; the rest of the catalog stays usable. `services.test.ts` holds the bundle at none, and holds every layout of a packed C struct against the struct in `runit-esp/components/**/*.h` (its size, each field inside the member of its name).

## The connection

- **Opening is one platform call**: connect, the runIT service alone (no walk over every service), its characteristics in one call, and a subscription to the two notify characteristics the link uses (answers and telemetry `0xFFE1`, logs and errors `0xFFE3`). Nothing is probed or waited for. The browser runs one GATT operation at a time: they go through one chain.
- **A board known from an earlier visit** (`knownDevices()`, after a reload of the tab) is connectable only once the browser has seen it advertise: before that `connect()` fails with "Bluetooth Device is no longer in range". `WebBluetoothPlatform` answers that first failure by listening for one advertisement (`watchAdvertisements`, in a browser that has it; up to 10 s) and connecting again, so a reloaded page opens the board with no chooser. A browser without `watchAdvertisements` gives the first failure back with that said; one without `getDevices()` lists no boards (`remembersBoards` is false). In Chrome and Edge both are behind flags: "Use the new permissions backend for Web Bluetooth" and "Experimental Web Platform features". A reload drops the link with no goodbye: the board hears of it after about 3 s (Windows keeps the closed client's session), enters its safe state when the dead-man was armed with `on_link_lost`, and otherwise carries on.
- **A drop is reopened at once**: the first attempt has no delay (the board advertises again as soon as it drops), then 0, 100, 250, 500, 1000, 2000 ms, the last repeating, until `disconnect()` (`reconnect: { delaysMs, maxAttempts, openTimeoutMs }`; `reconnect: false` turns it off). An attempt that neither works nor fails is dropped after `openTimeoutMs`; one that finishes late is closed.
- **Commands and the drop**: a command on the wire fails `link-lost` (it may have run: only the caller knows if it can be repeated); one given while the link is coming back waits for it (`waitForLinkMs`, default 5 s); with no link to wait for it fails `not-connected`.
- **A link that is down though nothing said so**: a write that fails recycles the link at once, and `stallAfter` (3) timeouts in a row do too. After the board applies a GATT change (SYS_BLE.MD "Runtime changes") Windows invalidates the handles and reports no disconnect: every write fails with `The object has been closed`. The old link is closed before the new one opens (a platform that still finds the board connected would reuse it, and the late close would drop the new link). A command that fails `send-failed` did not run (a frame runs only when its last packet arrives) and can be repeated.
- **`reopen()`** drops and opens again, to pick up a changed GATT table (the new service is visible after it).
- **Writes**: one request is written as packets back to back under one chain, so frames never interleave (the firmware reassembles with one buffer). Each write is acknowledged (flow control; nothing is dropped). A packet is `packetBytes` long, header included: 512 by default, the longest value ATT allows (513 B is refused with `Invalid Attribute Value Length`), so a frame of up to 511 B is one write and the 512 B maximum two. Web Bluetooth does not show the MTU; the board offers 527 and a central that takes it (Windows does) needs no splitting.
- **Pipelining**: `maxInFlight` (4) commands at once. The board runs normal commands in order, so this saves a round trip per command and reorders nothing. An urgent request (`urgent: true`) goes before every normal one still queued and is written even when the window is full (STOP and the dead-man's heartbeat never wait behind commands the board is slow to answer); on the board it runs on the urgent task.
- **Streams**: every frame that is not an answer goes to `onStream` by its stream byte (without it), raw, with the time it arrived (`at`): `link.onStream((f) => f.name /* telemetry | logs | errors | events, undefined for an unpublished byte */, f.data, f.at)`; `link.on('errors', (data) => …)` for one stream. Nothing is interpreted: the errors and logs are bytes (decode an error record with `decodeErrorRecord`, `protocol/errors.ts`, an event with `decodeEvent`, `protocol/events.ts`). The link listens to `0xFFE1` (answers, telemetry, events) and `0xFFE3` (logs, errors) as the board binds its connectors. A packet out of order drops the rest of its frame up to the next FIRST, as the firmware does; `onDiagnostic` says so.

## The dead-man

The board enters its safe state (VM stopped, devices suspended: RUNIT.MD) when no frame came for the dead-man's timeout, and at once when the link drops if `on_link_lost` is set (SYS_SYSTEM.MD "Dead-man"). Any frame feeds it. `session/deadman.ts` keeps it:

```ts
const deadman = new DeadMan(link, { timeoutMs: 1500 })        // onLinkLost: true, heartbeatMs: a third of the timeout
deadman.onEvent((e) => …)   // armed | restarted { uptimeMs } | lapsed { reason: 'link-lost' | 'silence', silentMs } | failed { step, error }
await deadman.start()        // the DEADMAN write now (or when the link opens), then fed until stop()
await deadman.stop()         // the DEADMAN write of 0; stop({ disarm: false }) leaves it to time out
```

- **Armed** with the DEADMAN write (sent urgent) when the link opens and after every reconnect: the setting lives in the board's RAM and a restart forgets it.
- **Fed** by a PING (urgent) only when nothing was written for `heartbeatMs`; the check runs every half heartbeat, so a heartbeat of 1/1.5 of the timeout or more is refused.
- **After a reconnect**, PING's uptime says whether the board restarted (`restarted`: its program, the devices a project made and the dead-man are as at boot). If not, `lapsed` says the board has most likely entered its safe state: `link-lost` with `on_link_lost`, `silence` when the last write before the drop is a timeout old. A silence of a whole timeout while connected (a timer held back) is `lapsed` too. The board sends nothing when it enters its safe state: this is the app's inference.
- Commands and fields are the descriptor's (`contract_sys.json`), named once in `DEADMAN_COMMANDS`; `deadman.test.ts` holds them against the catalog.
- **Leaving the safe state** is the caller's choice, never automatic (motors move again):

```ts
await leaveSafeState(link, { start: engine.exec('normal') })   // SYS SAFE_LEAVE (else RESUME to every device, [05][01][FF FF][03 00]), then the EXEC given
```

  The program stays loaded through the safe state; after `restarted` it is gone: `runProject(link)` (the stored project) or `loadProgram` instead. A failed RESUME is thrown and the VM is not started. A critical device fault latched by the VM refuses every start (`ERR_VM_EXEC_FAULT_LATCHED`) until the caller, who shows it, sends `engine.exec('ackFault')`. The commands are the descriptors' (`contract_device.json`, `contract_vm.json`), named once in `SAFE_STATE_COMMANDS`; the device ID of the sweep is `SYS_DEVICE_ID_NONE` (`device_id` in `enums.json`).
- A browser throttles timers of a background tab (to once a second, and to once a minute after a while in Chrome): a timeout of a few seconds survives the first, not the second. The desktop app turns throttling off for its window (AGENTS.md "The desktop app").

## Telemetry and the remote control (with the engine)

Telemetry is the write command in the other direction (VM_CORE.MD "Telemetry is the same command"), so the engine decodes it and writes with the same records. The backend only carries the frames:

```ts
const reader = engine.telemetry(result)                               // result = engine.compile(project): ids and types of its variables
link.on('telemetry', (data) => { for (const v of reader.read(data)?.values ?? []) show(v.variable, v.value) })
await sendProgram(link, [engine.subscribe(ids)])                      // the board sends everything once, then what changed
await sendProgram(link, engine.write(result, [{ variable: 'x', value: 5 }]))   // a runtime write, applied at the next pass
const plan = engine.watch(compiledWithAllEnableOuts, 'flow')                    // what to subscribe to for a live view (When done, variables, block outputs): plan.ids, plan.targets
```

The board samples at the end of a pass; stepped by block (`EXEC block`) it also samples at every stop before a block, and `VM STATUS` `next_block` names the block the next `NEXT` runs (VM_CORE.MD "Remote"). Which stepping is selected is not in STATUS: after a requested pass the board is `VM_RUN_FROZEN` with the scan mode still selected, so a client keeps the mode it selected itself (the app: `useVm`).

Checked on a board: after SUBSCRIBE `x=0, y=3, z=0`; a write of `x=5` came back as `x=5, z=13` (an expression block `z = x * 2 + y`) about 17 ms later, a write of `7.5` as `z=18`. The captured frames are `tests/backend/fixtures/board-streams.json`.

## Records and the project

The board keeps commands in NVS as records (SYS_ACTIONS.MD) and a whole project as one stream of sections plus the compiled program (SYS_PROJECT.MD). `session/project.ts`:

```ts
await storeRecord(link, 10, [deviceFrame(defs, 1, IO_LEVEL, Uint8Array.of(22, 0))])   // a user action (a write of IO LEVEL), run later with runRecord(link, 10)
const start = engine.exec('normal')
await saveProject(link, {                                                                  // settings, devices, the program, then its start
  sections: [{ id: deviceRecordId(1), frames: [...] }],
  program: result.frames.map((f) => vmFrame(defs, f.op, f.payload)),
  start: [vmFrame(defs, start.op, start.payload)],
}, { boot: link.defs.projectBoot.run, onProgress })        // the boot option: none / load / run, done at a boot and right after the save
const info = await projectInfo(link)                       // { stored, boot, bank, crc, bytes }: the CRC and length of the stream saved
await loadProject(link); await runProject(link)            // LOAD: every section but the start (the program waits); RUN: the start too
await setProjectBoot(link, link.defs.projectBoot.load)     // PROJECT OPTIONS write
const stored = await readProject(link)                     // { info, sections, program, verified } read back from NVS
const recovered = engine.recover(stored.program.flatMap((f) => programFrameOf(defs, f) ?? []))   // the project, decompiled
```

`projectStream(defs, content)` builds the stream (`[u16 len][frame]` frames, a SECTION frame before each section, the program as section 0, then the start section `0xFFFFFFFF`, `SECTION_START`) and `crc32` is zlib's. A record holds writes only (`[key][dir][op][args]`, dir the write: the board records no read, SYS_ACTIONS.MD): `vmFrame`, `deviceFrame` and the section frames are writes; `programFrameOf` reads a VM write back. Checked on a board: the stream built here (one device section, one user action and a program) was accepted and `INFO` returned its length and CRC; reading every record back gave the frames that went in, so `verified` is true. The records of that run, in the frames with the direction byte, are `tests/backend/fixtures/stored-project.json`. **Every section is run at each start**, user actions included: an action that must wait for a call is stored with `storeRecord`, not as a section (SYS_PROJECT.MD). IDs 3 to 6 are the firmware's.

## Errors for a user

An error record (ERRORS.MD "Error record": the board's clock when it was reported (`ErrorRecord.time`: UTC ms; before 2020 the clock was never set and it is a time since the start), the command, then the chain from the route to the root cause) is read with what `errors.json` says about each tag: its message template, its fields, what an id field refers to (`id_kind`, `id_kind_by`), which kinds are numbered within another (`scope`, `of`), which nodes are context (severity NONE), and each node's severity, which the record does not carry. How errors.json gets them: templates/AGENTS.md "Errors".

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
- **Names**: the app's resolver first (`programRefs`: objects to variables or to the block output that owns them, accessors to the block input or enable that reads through them, blocks and their pins to block instances; `deviceRefs`: the project's devices), then the descriptors (device types, device commands, router keys, VM commands, contracts, block types, error tags), then the kind's alias and the number (`Pin 22`, `I2C address 0x3C` for a kind with `format: hex`). An enum field shows its member's alias; a field with a unit shows it.
- A tag errors.json does not have keeps its number (`known: false`).

`ErrorLog` keeps them for the user (an entry is dated by its record's clock: `setClock(unixMs, uptimeMs)` tells it the board's clock so a time since the start becomes UTC; a record read again, as the board's history is each time the link comes up, is not counted twice):

```ts
const log = new ErrorLog(interpreter, { capacity: 200 })
log.attach(link)                                  // the errors stream; log.addCommandError(e) for a failed command
log.entries({ read: false, ref: 'device:3' })     // newest first; a device takes its pins' and channels' errors
log.markRead({ ref: 'device:3' }); log.clearRead() // or log.clear({ ref }) when the user fixed it
log.onChange((entries) => …)
```

- The board sends a command's error twice (the answer and the errors stream) and never suppresses repeats: the same record (link, seq and chain) within `duplicateMs` (2 s) is one; the same chain again is the same entry, its `count` and `last` go up and it is unread again. Past `capacity` the oldest read entries go first.
- What the backend knows by itself: the record's layout (from `enums.json`), which descriptors name which kinds (`device_type`, `device_op`, `router_key`, `vm_op`, `contract`, `vm_block_type`, `error_tag` in `ErrorInterpreter`) and how the program's kinds map to the project (`vm_object`, `vm_accessor`, `vm_block` and its pins, `device`, `pin`, `channel` in `errors/program.ts`). Messages, kinds, scopes and field meanings are errors.json's.

## Sending a list in order, loading a program

`sendProgram(link, frames)` takes the frames the engine compiles (`{ op, payload }`, commands of the VM router) and `sendInOrder(link, requests)` any list of requests. Up to `window` (4) are on the wire at once, every answer is awaited and returned in the order of the requests, `onProgress(done, total)` follows each. The first request that fails stops the sending (nothing after it is sent: a block would not load without its objects), the ones already on the wire are awaited, and the link's own `CommandError` is thrown as it is. It neither retries nor cleans up: runtime writes, SUBSCRIBE and EXEC go this way.

`loadProgram(link, frames)` loads a program: a list that starts with OPEN (EXEC may follow). The board fails closed (VM_CORE.MD "Fail closed": a refused load op discards the whole program, the upload restarts at OPEN), so:

```ts
await loadProgram(link, [...result.frames, engine.exec('normal')], {
  retries: 2,                                    // default
  onRetry: (attempt, error) => …,                // before attempt 2, 3, …
})                                               // the answers of the attempt that worked; or throws LoadError
// LoadError: { cause, attempts, answered (frames answered in the last attempt), reset: 'done' | 'failed' | 'skipped', resetError? }
```

- **A retry is the whole load, from OPEN** (OPEN replaces what the board holds), never the frame that failed: one that timed out or was on the wire when the link dropped may have run, and its objects would be added twice. `onProgress` starts again from 0.
- **Only a failure of the link is retried**: `timeout`, `link-lost`, `send-failed`. The board's refusal (`device-error`) and the rest would come again.
- **When it gives up, RESET** (`reset: false` leaves the board as it is): a load cut off by the link stays half-loaded on the board; a refused one is already discarded, and RESET leaves it so. `reset: 'failed'` (no link) means the board may still hold a half-loaded program, which its first start refuses (`vm_loader_verify_complete`).
- The commands are the descriptor's (`contract_vm.json`), named once in `LOAD_COMMANDS`; `send.test.ts` holds them against the catalog and checks that the engine's load starts with OPEN.

## Where each fact comes from

| Fact | Source | Held against it by |
|---|---|---|
| Router keys, flags, status, stream bytes | `enums.json` (`router_key`, `router_flag`, `router_status_e`, `stream_byte`) | `protocol.test.ts` reads `router_ids.h` and the router Kconfig |
| GATT UUIDs, fragment bits | `enums.json` (`ble_runit_uuid`, `ble_frag`, marked in `sys_ble.h`) | `protocol.test.ts` reads `sys_ble.h`; the framing tests use the examples of SYS_BLE.MD |
| Frame limit | `limits.json` (`ROUTER_FRAME_MAX`) | `protocol.test.ts` reads the router Kconfig |
| Error record | `ROUTER.MD`, `errors.json` | `protocol.test.ts` decodes the example of ROUTER.MD |
| OPEN and RESET of the VM router (`loadProgram`) | `generated/services/contract_vm.json` | `send.test.ts`: the catalog has them, the engine's load starts with that OPEN |
| Ops of the `actions` and `project` routers | `generated/services/contract_actions.json`, `contract_project.json` | `project.test.ts`: the requests carry the ops and payload layouts the board accepted |
| The board's devices, their pins and I2C places; the onboard action IDs | `generated/common/board.json` (the PCB), `board_devkit.json` (a devkit); `limits.json` (`RUNIT_ACTION_ID_*`, marked in `runit-esp/components/app/runit/Kconfig`) | `tests/app/devices.test.ts`: every board device has a descriptor, the pins it uses; onboard IDs never taken by an action; `tests/app/power.test.ts`: the devkit board has device 0 only and no power layout |
| Every other command, its arguments and answer | `generated/services`, `generated/contracts`, `generated/devices`; `device_contract`, `device_id`, `io_pin_none` in `enums.json` | `services.test.ts`: no problems in the bundle; every packed struct's layout against its C typedef; a device request equals the frame of the stored-project fixture; `sys_io_pin_t` is the 3 B of a pin reference |
| The events stream: the NOTIFY op, `sys_event_t`, the device contract's events | `enums.json` (`event_op`), `contract_event.json`, `contract_device.json` | `events.test.ts`: `sys_event_t` is 18 B, an event read with its contract and name, the DEVICE events equal `SYS_DEVICE_EVENTS` of `sys_device.h` |
| Error messages, what an error field refers to | `errors.json` (`message`, `id_kind`, `id_kind_by`, `of`, `context`, `id_kinds`), from the maps and `errors_<module>.json` | `errors.test.ts`: every placeholder is a field, every kind is described; records built from errors.json read back with names |
| The way out of the safe state | `SYS_SYSTEM.MD` (SAFE_LEAVE), `SYS_DEVICE.MD` "Sweeps over the router", `RUNIT.MD` "Safe state", VM_CORE.MD | `safestate.test.ts`: SAFE_LEAVE first, the RESUME sweep (the frame of SYS_DEVICE.MD) when it is refused |
| The stored project's records, the stream | `SYS_PROJECT.MD`, `SYS_ACTIONS.MD` | `fixtures/stored-project.json`: the stream, its CRC and every record read back from a board |

## Tests

`npm test` from `runit-app/`. Fixtures captured from a board: `tests/backend/fixtures/board-streams.json` (telemetry and log frames), `stored-project.json` (a saved project and its records). `tests/backend/fakes.ts` is a board behind a fake platform (reassembles what is written, answers on notify, drops the link, fails or hangs an open). `link.test.ts` covers requests, order, parts, errors, timeouts, streams, reconnecting, the stall and the late open.

**The backend on a board:** `npm run test:hw` (`vitest.hardware.config.ts`, `tests/hardware/*.hw.test.ts`) runs this TypeScript backend itself, the real `RunitLink`, `Services`, `DeadMan`, `ErrorLog`, `loadProgram` and `saveProject`, against a board over the PC's Bluetooth. Node has no BLE, so `tests/hardware/bleakPlatform.ts` is a `BlePlatform` that moves packets through `bleak_bridge.py` (bleak, JSON lines on stdio); framing, commands and the connection logic stay the backend's. `uart_tap.py` reads the board's console beside it (`Console` in `uart.ts`): what the firmware logs is the independent witness for what the app infers (the safe state, a panic). Environment: `HW_UART` (console port, default `COM9`), `HW_NAME` (advertised name, default `runit`), `HW_ADDRESS`, `HW_PYTHON` (default `../.venv`, which has bleak and pyserial). One file at a time, in order:

| Suite | Covers |
|---|---|
| `a-link` | connect, MTU, 50 PINGs and 40 pipelined, an error answer and its copy on the errors stream, a long answer, 11 drops from the host (each reconnected), `reopen()`, a board restart |
| `b-services` | no catalog problems; every read-only command of every service decoded; the 13 onboard devices by type; IO on the status LED; a pin or device that is not there; SUSPEND / RESUME; CREATE with an id in use |
| `c-errors` | records captured from the board read for a user (the app's device names, the pin, the command); `ErrorLog`: answer and stream copy are one entry, repeats count up, read / clear by device, a burst of 20 |
| `d-deadman` | `DeadMan` armed, fed past its timeout and disarmed; the safe state after silence, from STOP and on a lost link (held against the console); `leaveSafeState`; `lapsed` and `restarted` inferred after a drop and a restart |
| `e-program` | the engine's program loaded with `loadProgram`, run, SUBSCRIBE, runtime writes as telemetry (latency), pause / resume, a program the board refuses (`LoadError`, RESET, a readable error), a good load after it |
| `g-refs` | errors tied back to the user's names: a program refused at load ("input 2 of Compute does not exist", "output 1 of Constant refers to Result, which does not exist", marked on that block pin), a block failing while it runs ("Expander has no Pin 99", marked on the block, the device and the pin; "Device 77 does not exist"), a command to a device (pin, DAC channel, unknown op), a write beyond a variable (marked on the variable); never an `obj-…` id; `ErrorLog` by device and pin |
| `f-project` | a stored record (store, list, read back, run, replace, remove); `saveProject` with INFO's length and CRC, `readProject` verified, the engine recovering the project, `loadProject` (the program waits), `runProject`, boot RUN across a restart, `eraseProject` |

The suites change the board and put it back: the status LED and expander pin 22 are toggled, devices are suspended and resumed, the VM program is replaced, record 90 and a project are stored and erased, the dead-man is left disarmed, and the board restarts three times. No other BLE client may hold the board, and nothing else may hold the console port.

**In a browser:** `npm run test:browser` runs `WebBluetoothPlatform` itself, the platform the app ships, in Chrome or Edge (`tests/browser/`: `main.ts` is the suite in the page, `run.mjs` the runner). Vite serves the page; the runner drives the browser over the DevTools protocol with Node's own WebSocket and answers the Web Bluetooth chooser through the `DeviceAccess` domain (it takes the board named `HW_NAME`, default `runit`, once the scan shows it: nobody clicks). `CHROME` picks the browser (default Chrome, else Edge; `CDP_PORT` if 9333 is taken). It covers connect, the catalog, 90 PINGs, the errors stream, IO, a program with telemetry, the dead-man, a stored project, the way out of the safe state, `reopen()` and a board restart with the link coming back by itself; then reloading the tab: with the dead-man armed (the new page connects with no chooser, the uptime goes on, the board is in its safe state, `leaveSafeState` restarts the loaded program), with nothing armed (no safe state), and three reloads in a row. The runner starts the browser with `--enable-features=WebBluetoothNewPermissionsBackend` and `--enable-experimental-web-platform-features` (`getDevices()` and `watchAdvertisements`). A failed run can leave the board armed or in its safe state: the suite disarms and resumes it first. Needs no other BLE client on the board; the board restarts once.

## Measured on a board (Windows 11, Intel BT, `bleak` driving the same framing and router)

| What | Result |
|---|---|
| MTU | 527 on the client, and the board's `STATUS` agrees |
| Connect + subscribe | 0.6-0.9 s (the very first connection of a PC: 3 s) |
| `PING` round trip | median 15 ms, p95 about 35 ms; 40 pipelined in 0.65 s (the OS runs acknowledged writes one at a time) |
| A 512 B request | 411 ms in packets of 20, 105 ms of 100, 60 ms of 240, 48 ms of 512 (two writes); about 9.7 KB/s back to back |
| Frames of 13-503 B in packets of 20, 100, 240, 512 | all reassembled and routed; the board's long answers (`LIST`) arrive whole, no dropped packets |
| 10 disconnect + connect cycles, each with a `PING` | 10/10, median 361 ms, worst 615 ms |
| Board restart (`sys` RESTART) | the loss is noticed after about 2.1 s (the supervision timeout), the first reconnect attempt then works: 2.7 s from the drop |
| Idle 45 s, then `PING` | link up, 81 ms |
| 300 mixed requests (ping, list, status, oversized, bursts of 8) | 0 failures, 0 dropped packets, `rx_overflow` 0 |
| `APPLY` with a client connected | Windows reports no disconnect and every later write fails; reopening shows the new service |

## Not verified / not done

- The TypeScript link, `Services`, `saveProject` / `readProject`, `loadProgram`, `DeadMan`, `leaveSafeState`, `ErrorInterpreter` and `ErrorLog` have run against a board (`npm run test:hw`) through bleak on Windows. `WebBluetoothPlatform` has run in Chrome and Edge on Windows (`npm run test:browser`); Android is untried.
- An abrupt loss of the radio (not a disconnect, not a restart) is untried.
- The link has no heartbeat of its own: a silent link is found by the write and stall rules or by the OS supervision timeout (2 s on the firmware side); `DeadMan`'s PING only runs while it is started. The board does not report entering its safe state, so `lapsed` is inferred (`VM STATUS`, VM_CORE.MD, reads the VM's run mode and whether a program is loaded, but not whether the devices are suspended); the hardware suite holds it against the console. With `on_link_lost` the board logs the safe state about 3 s after a client closed the link: Windows keeps the GATT session of a closed client until its supervision timeout, so the board hears of the loss late.
- Web Bluetooth cannot read the MTU, the RSSI or the connection parameters; a native platform can implement `BlePlatform` and give them.
- Only BLE. A UART or Wi-Fi platform would implement the same channel; `RunitLink` fragments for BLE only.
- The messages of `ErrorInterpreter` are English. The records of the PCB's blocks and devices read right (`g-refs`); the refs of a block that is a span owner (FOR) and of a heap object are not exercised on a board.
- Answers whose layout a descriptor gives only in words (`CONNECTOR INFO`, `BLE LIST`, the logs stream) are not decoded past their leading value.
