# The backend: the link to a board (for agents)

`runit-app/src/backend/`: commands, streams and a connection that keeps itself up, over BLE. No UI, no DOM beyond the Web Bluetooth platform. Written from the firmware (`components/sys/sys_ble/`, `components/core/router/`) and `generated/common/`.

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
| `definitions.ts` | The only file that reads `generated/common/`: router keys, flags, status, stream bytes, the runIT GATT UUIDs and the fragment bits (`ble_runit_uuid`, `ble_frag` in `enums.json`), `ROUTER_FRAME_MAX` (`limits.json`), the error tags (`errors.json`). A symbol the firmware stops publishing stops the load with a message that names it. |
| `protocol/` | `frames.ts`: request `[seq][flags][keys…][payload]`, response `[stream][seq][status][data]` (ROUTER.MD). `errors.ts`: the error record (source, command, chain of tags with their fields, named from `errors.json`). |
| `transport/` | `types.ts`: what a BLE platform provides (`requestDevice`, `knownDevices`, `open` → a channel to write packets to, packets and a loss notice back). `framing.ts`: a frame to packets and back, as the firmware does it (SYS_BLE.MD "Framing"). `webBluetooth.ts`: the Web Bluetooth platform. |
| `session/` | `commands.ts`: sequence bytes, answers by seq, timeouts, pipelining, answers in parts. `link.ts`: `RunitLink`, the connection. `send.ts`: `sendInOrder` / `sendProgram`, a list of requests sent in order. `project.ts`: stored records, the project stream, reading a project back. |

## The connection

- **Opening is one platform call**: connect, the runIT service alone (no walk over every service), its characteristics in one call, and a subscription to the two notify characteristics the link uses (answers and telemetry `0xFFE1`, logs and errors `0xFFE3`). Nothing is probed or waited for. The browser runs one GATT operation at a time: they go through one chain.
- **A drop is reopened at once**: the first attempt has no delay (the board advertises again as soon as it drops), then 0, 100, 250, 500, 1000, 2000 ms, the last repeating, until `disconnect()` (`reconnect: { delaysMs, maxAttempts, openTimeoutMs }`; `reconnect: false` turns it off). An attempt that neither works nor fails is dropped after `openTimeoutMs`; one that finishes late is closed.
- **Commands and the drop**: a command on the wire fails `link-lost` (it may have run: only the caller knows if it can be repeated); one given while the link is coming back waits for it (`waitForLinkMs`, default 5 s); with no link to wait for it fails `not-connected`.
- **A link that is down though nothing said so**: a write that fails recycles the link at once, and `stallAfter` (3) timeouts in a row do too. After the board applies a GATT change (SYS_BLE.MD "Runtime changes") Windows invalidates the handles and reports no disconnect: every write fails with `The object has been closed`. The old link is closed before the new one opens (a platform that still finds the board connected would reuse it, and the late close would drop the new link). A command that fails `send-failed` did not run (a frame runs only when its last packet arrives) and can be repeated.
- **`reopen()`** drops and opens again, to pick up a changed GATT table (the new service is visible after it).
- **Writes**: one request is written as packets back to back under one chain, so frames never interleave (the firmware reassembles with one buffer). Each write is acknowledged (flow control; nothing is dropped). A packet is `packetBytes` long, header included: 512 by default, the longest value ATT allows (513 B is refused with `Invalid Attribute Value Length`), so a frame of up to 511 B is one write and the 512 B maximum two. Web Bluetooth does not show the MTU; the board offers 527 and a central that takes it (Windows does) needs no splitting.
- **Pipelining**: `maxInFlight` (4) commands at once. The board runs normal commands in order, so this saves a round trip per command and reorders nothing; an urgent request (`urgent: true`) overtakes.
- **Streams**: every frame that is not an answer goes to `onStream` by its stream byte (without it), raw, with the time it arrived (`at`): `link.onStream((f) => f.name /* telemetry | logs | errors | events, undefined for an unpublished byte */, f.data, f.at)`; `link.on('errors', (data) => …)` for one stream. Nothing is interpreted: the errors and logs are bytes (decode an error record with `decodeErrorRecord`, `protocol/errors.ts`). The link listens to `0xFFE1` (answers, telemetry, events) and `0xFFE3` (logs, errors) as the board binds its connectors. A packet out of order drops the rest of its frame up to the next FIRST, as the firmware does; `onDiagnostic` says so.

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

## Tests

`npm test` from `runit-app/`. On a board: `.claude/skills/runit-esp/scripts/ble_suite.py` (build.md §6) drives the same protocol from a PC; its `load` scenario sends frames the engine compiled (a JSON of `{op, payload}`) to a real board, `capture` also applies runtime writes and records every stream raw. `tests/backend/fakes.ts` is a board behind a fake platform (reassembles what is written, answers on notify, drops the link, fails or hangs an open). `link.test.ts` covers requests, order, parts, errors, timeouts, streams, reconnecting, the stall and the late open.

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

- The TypeScript link itself has run against a fake platform only (`tests/backend/`); the numbers above are the board's behaviour as seen through the same protocol. Web Bluetooth in a browser, and Android, are untried.
- An abrupt loss of the radio (not a disconnect, not a restart) is untried.
- No heartbeat: a silent link is found by the write and stall rules or by the OS supervision timeout (2 s on the firmware side). The dead-man (`sys` DEADMAN_SET) is the firmware's own check of the app.
- Web Bluetooth cannot read the MTU, the RSSI or the connection parameters; a native platform can implement `BlePlatform` and give them.
- Only BLE. A UART or Wi-Fi platform would implement the same channel; `RunitLink` fragments for BLE only.
