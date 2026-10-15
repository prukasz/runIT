# The backend: the link to a board (for agents)

`runit-app/src/backend/`: commands, streams and a connection that keeps itself up, over BLE. No UI, no DOM beyond the Web Bluetooth platform. Written from the firmware (`components/sys/sys_ble/`, `components/core/router/`) and `generated/`.

```ts
const link = new RunitLink(new WebBluetoothPlatform(bundledLinkDefinitions()))
await link.connect(await platform.requestDevice())            // a user gesture; or platform.knownDevices() with no chooser
link.onStatus((s) => …)                                       // { state, reason?, attempt? }
link.onStream((f) => …)                                       // telemetry / logs / errors / events: { stream, name, data }
const out = await link.request({ keys: [defs.keys.sys, 0x01] }) // PING; keys = root key, then the op
await sendProgram(link, engine.compile(project).frames, { onProgress: (done, total) => … })   // in order, each answered
```

## Layout (dependencies point down)

| Module | What it is |
|---|---|
| `definitions.ts` | The only file that reads `generated/`: router keys, flags, status, stream bytes, the runIT GATT UUIDs and the fragment bits (`ble_runit_uuid`, `ble_frag` in `enums.json`), `ROUTER_FRAME_MAX` (`limits.json`), the error tags (`errors.json`); and every service, contract and device descriptor (`bundledServiceCatalog`, a glob: a new file is picked up). A symbol the firmware stops publishing stops the load with a message that names it. |
| `catalog/` | `services.ts`: `ServiceCatalog`, every command of every router as the descriptors describe it, and what does not hold together in them (`problems`). `codec.ts`: a payload from field values and back, by the descriptor's layout. |
| `protocol/` | `frames.ts`: request `[seq][flags][keys…][payload]`, response `[stream][seq][status][data]` (ROUTER.MD). `errors.ts`: the error record (source, command, chain of tags with their fields, named from `errors.json`). `services.ts`: the request of any catalog command and its answer decoded. |
| `transport/` | `types.ts`: what a BLE platform provides (`requestDevice`, `knownDevices`, `open` → a channel to write packets to, packets and a loss notice back). `framing.ts`: a frame to packets and back, as the firmware does it (SYS_BLE.MD "Framing"). `webBluetooth.ts`: the Web Bluetooth platform. |
| `session/` | `commands.ts`: sequence bytes, answers by seq, timeouts, pipelining, answers in parts. `link.ts`: `RunitLink`, the connection. `send.ts`: `sendInOrder` / `sendProgram`, a list of requests sent in order. `services.ts`: `Services`, the catalog's commands over the link. `deadman.ts`: `DeadMan`, the board's dead-man armed and fed. `project.ts`: stored records, the project stream, reading a project back. |

## Every command, from the descriptors

The backend names no service, contract, command or field: it reads them from the descriptors the firmware publishes (`templates/contract.schema.json`, `device.schema.json`). How an entry is addressed follows from what its descriptor has:

| Entry | Has | Request |
|---|---|---|
| service | `router_key` (`generated/services`) | `[router key][op][args]` |
| device contract | `contract_id`, no router key (`generated/contracts`) | `[device key][device id u16][(contract id << 8) \| op u16][args]` (SYS_DEVICE.MD "Ops") |
| device type | `type_id` (`generated/devices`) | its own ops in contract `SYS_DEVICE_CONTRACT_OWN`; CREATE (device contract `SYS_DEVICE_CONTRACT_DEVICE`) carries `[type u16][config]` |

```ts
const services = new Services(link)                                              // the bundled catalog by default
const { value } = await services.call('SYS', 'PING')                             // { uptime_ms: 1234 }
await services.call('SYS', 'DEADMAN_SET', { timeout_ms: 500, on_link_lost: true })
await services.device(3, 'IO', 'SET_LEVEL', { pin: 22, level: 'High' })         // a choice by its label, an enum by its member name
await services.device(9, 'servo', 'set', { value_x10: 900 })                     // a device type's own op
await services.create(20, 'servo', { … })                                         // the fields of device_servo.json's config
storeRecord(link, 10, [requestFrame(deviceCall(defs, catalog, 3, 'IO', 'TOGGLE', { pin: 22 }).request)])   // the same request in a record
```

- **Layout**: a field sits at its `offset` (or after the one before), is `size` bytes (or its `raw_type`'s width); a number field of several widths is an array (`u32` of 16 B is a list of 4); a `bytes` / `string` field without a size runs to the end and is last; a `string` ends in one NUL (`cmd_name_arg`). Little endian.
- **Values** are given by field id. A missing field takes its `default`, an optional one zero; else the call throws `PayloadError`, as it does for a field id that is not there, a value outside the raw type or the descriptor's `min` / `max`, and an unknown choice label or enum member. A command marked `urgent` is sent urgent.
- **Answers** are decoded by `returns`: a struct by field id (booleans as booleans), a value, or a list of either. A value that only leads the answer (a count, the rest described in words: `CONNECTOR INFO`, `BLE LIST`) is read from the front; `Answer.data` keeps the bytes.
- **Problems**: overlapping fields, a field that does not fit its raw type, a router key or contract id the firmware does not publish (`router_key`, `device_contract` in `enums.json`), an op twice, a device naming a contract nobody describes. They go to `catalog.problems`; the rest of the catalog stays usable. `services.test.ts` holds the bundle at none, and holds every layout of a packed C struct against the struct in `components/**/*.h` (its size, each field inside the member of its name).

## The connection

- **Opening is one platform call**: connect, the runIT service alone (no walk over every service), its characteristics in one call, and a subscription to the two notify characteristics the link uses (answers and telemetry `0xFFE1`, logs and errors `0xFFE3`). Nothing is probed or waited for. The browser runs one GATT operation at a time: they go through one chain.
- **A drop is reopened at once**: the first attempt has no delay (the board advertises again as soon as it drops), then 0, 100, 250, 500, 1000, 2000 ms, the last repeating, until `disconnect()` (`reconnect: { delaysMs, maxAttempts, openTimeoutMs }`; `reconnect: false` turns it off). An attempt that neither works nor fails is dropped after `openTimeoutMs`; one that finishes late is closed.
- **Commands and the drop**: a command on the wire fails `link-lost` (it may have run: only the caller knows if it can be repeated); one given while the link is coming back waits for it (`waitForLinkMs`, default 5 s); with no link to wait for it fails `not-connected`.
- **A link that is down though nothing said so**: a write that fails recycles the link at once, and `stallAfter` (3) timeouts in a row do too. After the board applies a GATT change (SYS_BLE.MD "Runtime changes") Windows invalidates the handles and reports no disconnect: every write fails with `The object has been closed`. The old link is closed before the new one opens (a platform that still finds the board connected would reuse it, and the late close would drop the new link). A command that fails `send-failed` did not run (a frame runs only when its last packet arrives) and can be repeated.
- **`reopen()`** drops and opens again, to pick up a changed GATT table (the new service is visible after it).
- **Writes**: one request is written as packets back to back under one chain, so frames never interleave (the firmware reassembles with one buffer). Each write is acknowledged (flow control; nothing is dropped). A packet is `packetBytes` long, header included: 512 by default, the longest value ATT allows (513 B is refused with `Invalid Attribute Value Length`), so a frame of up to 511 B is one write and the 512 B maximum two. Web Bluetooth does not show the MTU; the board offers 527 and a central that takes it (Windows does) needs no splitting.
- **Pipelining**: `maxInFlight` (4) commands at once. The board runs normal commands in order, so this saves a round trip per command and reorders nothing. An urgent request (`urgent: true`) goes before every normal one still queued and is written even when the window is full (STOP and the dead-man's heartbeat never wait behind commands the board is slow to answer); on the board it runs on the urgent task.
- **Streams**: every frame that is not an answer goes to `onStream` by its stream byte (without it), raw, with the time it arrived (`at`): `link.onStream((f) => f.name /* telemetry | logs | errors | events, undefined for an unpublished byte */, f.data, f.at)`; `link.on('errors', (data) => …)` for one stream. Nothing is interpreted: the errors and logs are bytes (decode an error record with `decodeErrorRecord`, `protocol/errors.ts`). The link listens to `0xFFE1` (answers, telemetry, events) and `0xFFE3` (logs, errors) as the board binds its connectors. A packet out of order drops the rest of its frame up to the next FIRST, as the firmware does; `onDiagnostic` says so.

## The dead-man

The board enters its safe state (VM stopped, devices suspended: RUNIT.MD) when no frame came for the dead-man's timeout, and at once when the link drops if `on_link_lost` is set (ROUTER.MD "Safe state and dead-man"). Any frame feeds it. `session/deadman.ts` keeps it:

```ts
const deadman = new DeadMan(link, { timeoutMs: 1500 })        // onLinkLost: true, heartbeatMs: a third of the timeout
deadman.onEvent((e) => …)   // armed | restarted { uptimeMs } | lapsed { reason: 'link-lost' | 'silence', silentMs } | failed { step, error }
await deadman.start()        // DEADMAN_SET now (or when the link opens), then fed until stop()
await deadman.stop()         // DEADMAN_SET 0; stop({ disarm: false }) leaves it to time out
```

- **Armed** with DEADMAN_SET (sent urgent) when the link opens and after every reconnect: the setting lives in the board's RAM and a restart forgets it.
- **Fed** by a PING (urgent) only when nothing was written for `heartbeatMs`; the check runs every half heartbeat, so a heartbeat of 1/1.5 of the timeout or more is refused.
- **After a reconnect**, PING's uptime says whether the board restarted (`restarted`: its program, the devices a project made and the dead-man are as at boot). If not, `lapsed` says the board has most likely entered its safe state: `link-lost` with `on_link_lost`, `silence` when the last write before the drop is a timeout old. A silence of a whole timeout while connected (a timer held back) is `lapsed` too. The board sends nothing when it enters its safe state: this is the app's inference.
- Commands and fields are the descriptor's (`contract_sys.json`), named once in `DEADMAN_COMMANDS`; `deadman.test.ts` holds them against the catalog.
- A browser throttles timers of a background tab (to once a second, and to once a minute after a while in Chrome): a timeout of a few seconds survives the first, not the second.

## Telemetry and the remote control (with the engine)

Telemetry is the write command in the other direction (VM_CORE.MD "Telemetry is the same command"), so the engine decodes it and writes with the same records. The backend only carries the frames:

```ts
const reader = engine.telemetry(result)                               // result = engine.compile(project): ids and types of its variables
link.on('telemetry', (data) => { for (const v of reader.read(data)?.values ?? []) show(v.variable, v.value) })
await sendProgram(link, [engine.subscribe(ids)])                      // the board sends everything once, then what changed
await sendProgram(link, engine.write(result, [{ variable: 'x', value: 5 }]))   // a runtime write, applied at the next pass
```

Checked on a board: after SUBSCRIBE `x=0, y=3, z=0`; a write of `x=5` came back as `x=5, z=13` (an expression block `z = x * 2 + y`) about 17 ms later, a write of `7.5` as `z=18`. The captured frames are `tests/backend/fixtures/board-streams.json`.

## Records and the project

The board keeps commands in NVS as records (SYS_ACTIONS.MD) and a whole project as one stream of sections plus the compiled program (SYS_PROJECT.MD). `session/project.ts`:

```ts
await storeRecord(link, 10, [deviceFrame(defs, 1, IO_SET_LEVEL, Uint8Array.of(22, 0))])   // a user action, run later with runRecord(link, 10)
await saveProject(link, {                                                                  // settings, devices, then the program (last)
  sections: [{ id: deviceRecordId(1), frames: [...] }],
  program: [...result.frames, engine.exec('normal')].map((f) => vmFrame(defs, f.op, f.payload)),
}, { autostart: true, onProgress })
const info = await projectInfo(link)                       // { stored, autostart, crc, bytes }: the CRC and length of the stream saved
const stored = await readProject(link)                     // { info, sections, program, verified } read back from NVS
const recovered = engine.recover(stored.program.flatMap((f) => programFrameOf(defs, f) ?? []))   // the project, decompiled
```

`projectStream(defs, content)` builds the stream (`[u16 len][frame]` frames, a SECTION frame before each section, the program as section 0 last) and `crc32` is zlib's. Checked on a board: the stream built here (297 B, one device section, one user action and a program) was accepted and `INFO` returned its length and CRC; reading every record back gave the frames that went in, so `verified` is true. The records of that run are `tests/backend/fixtures/stored-project.json`. **Every section is run at each start**, user actions included: an action that must wait for a call is stored with `storeRecord`, not as a section (SYS_PROJECT.MD). IDs 3 to 6 are the firmware's.

## Sending a list in order

`sendProgram(link, frames)` takes the frames the engine compiles (`{ op, payload }`, commands of the VM router) and `sendInOrder(link, requests)` any list of requests. Up to `window` (4) are on the wire at once, every answer is awaited and returned in the order of the requests, `onProgress(done, total)` follows each. The first request that fails stops the sending (nothing after it is sent: a block would not load without its objects), the ones already on the wire are awaited, and the link's own `CommandError` is thrown as it is. Retrying and cleaning up after a failure (a reset of the half-loaded program) are not done yet.

## Where each fact comes from

| Fact | Source | Held against it by |
|---|---|---|
| Router keys, flags, status, stream bytes | `enums.json` (`router_key`, `router_flag`, `router_status_e`, `stream_byte`) | `protocol.test.ts` reads `router_ids.h` and the router Kconfig |
| GATT UUIDs, fragment bits | `enums.json` (`ble_runit_uuid`, `ble_frag`, marked in `sys_ble.h`) | `protocol.test.ts` reads `sys_ble.h`; the framing tests use the examples of SYS_BLE.MD |
| Frame limit | `limits.json` (`ROUTER_FRAME_MAX`) | `protocol.test.ts` reads the router Kconfig |
| Error record | `ROUTER.MD`, `errors.json` | `protocol.test.ts` decodes the example of ROUTER.MD |
| Ops of the `actions` and `project` routers | `generated/services/contract_actions.json`, `contract_project.json` | `project.test.ts`: the requests carry the ops and payload layouts the board accepted |
| Every other command, its arguments and answer | `generated/services`, `generated/contracts`, `generated/devices`; `device_contract` in `enums.json` | `services.test.ts`: no problems in the bundle; every packed struct's layout against its C typedef; a device request equals the frame of the stored-project fixture |
| The stored project's records, the stream | `SYS_PROJECT.MD`, `SYS_ACTIONS.MD` | `fixtures/stored-project.json`: the stream, its CRC and every record read back from a board |

## Tests

`npm test` from `runit-app/`. Fixtures captured from a board: `tests/backend/fixtures/board-streams.json` (telemetry and log frames), `stored-project.json` (a saved project and its records). On a board: `.claude/skills/runit-esp/scripts/ble_suite.py` (build.md §6) drives the same protocol from a PC; its `load` scenario sends frames the engine compiled (a JSON of `{op, payload}`) to a real board, `capture` also applies runtime writes and records every stream raw. `tests/backend/fakes.ts` is a board behind a fake platform (reassembles what is written, answers on notify, drops the link, fails or hangs an open). `link.test.ts` covers requests, order, parts, errors, timeouts, streams, reconnecting, the stall and the late open.

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

- The TypeScript link itself, including `saveProject` / `readProject`, has run against a fake platform only (`tests/backend/`); the stream it builds was sent to a board by a script and every record read back; the numbers above are the board's behaviour as seen through the same protocol. Web Bluetooth in a browser, and Android, are untried.
- An abrupt loss of the radio (not a disconnect, not a restart) is untried.
- The link has no heartbeat of its own: a silent link is found by the write and stall rules or by the OS supervision timeout (2 s on the firmware side); `DeadMan`'s PING only runs while it is started. `DeadMan` has run against a fake platform only; the board does not report entering its safe state, so `lapsed` is inferred, and leaving the safe state (resuming devices, running the program again) is the caller's.
- Web Bluetooth cannot read the MTU, the RSSI or the connection parameters; a native platform can implement `BlePlatform` and give them.
- Only BLE. A UART or Wi-Fi platform would implement the same channel; `RunitLink` fragments for BLE only.
- `Services` has run against a fake platform only. Answers whose layout a descriptor gives only in words (`CONNECTOR INFO`, `BLE LIST`, the logs and events streams) are not decoded past their leading value. A pin reference field (`pin_ref`, a `sys_io_pin_t`) is given as its 3 bytes. `inherits` of a contract is not followed.
