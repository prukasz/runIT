# BLE transport backend

`src/backend/ble` is a protocol-agnostic raw BLE transport boundary.  It does
not know runIT packet classes, device configuration, VM objects, or UI state.
Those layers select a discovered characteristic and pass raw `Uint8Array`
payloads to it.

The boundary supports interactive device selection, connection lifecycle,
GATT rediscovery, characteristic read/write, notification subscriptions,
disconnection events, pairing capability reporting, and negotiated-MTU
reporting.  An adapter must report an unavailable platform capability rather
than pretend it completed the operation.

The initial adapter is Web Bluetooth, usable from Chromium-based desktop
browsers on Windows and Linux.  It uses the browser's chooser and the host
pairing UI.  Web Bluetooth deliberately exposes neither passkey entry nor
MTU control/inspection, so both are reported as host-managed/unavailable.
This also matches the current runIT firmware, whose NimBLE configuration uses
no bonding or MITM authentication.  A later native Windows/Linux or Capacitor
adapter can implement these optional capabilities without changing callers.

The backend discovers the GATT topology instead of hard-coding it. Which runIT
characteristics carry which stream comes from the generated stream catalog
(see "Firmware IDs come from the generated JSON" below).

## Desktop test console

The temporary `BleTestApp.tsx` is a manual integration console for the Web
Bluetooth adapter. It selects a runIT-advertising device, connects,
rediscovers its GATT database, reads or subscribes to individual
characteristics, and writes explicit hexadecimal bytes. After GATT discovery it
opens a runIT session and adds the command console and the Errors and Logs
panels (see below). It has no device state and no VM/device screens.

Web Bluetooth does not expose GATT descriptor enumeration or direct descriptor
reads/writes. Discovery therefore adds an **inferred** Client Characteristic
Configuration Descriptor (CCCD, `0x2902`) to every notify/indicate
characteristic. `Subscribe` and `Unsubscribe` remain the only portable way to
change its value. Native adapters may replace inferred entries with enumerated
descriptor metadata later.

The raw BLE adapter deliberately returns platform-provided UUIDs only. The
test app names the runIT service and characteristics from the stream catalog,
by the streams the board binds to each one (e.g. "runIT notify: logs, errors"),
and shows both 16-bit and full UUID forms.

## Adapter observability

The BLE contract exposes connection-state events, link information, write-size
limits, operation counters, pairing state, and an explicit forget-device
operation. Values that Web Bluetooth hides (MTU, RSSI, PHY, connection
interval, and real pairing state) are `null` or unavailable, never guessed.
Round-trip latency and command retries remain protocol concerns: only a runIT
request ID plus an acknowledgement can establish execution latency, and
repeating a raw write could duplicate a non-idempotent command.

## Runtime stream routing

`src/backend/stream` is the transport-neutral boundary for application data.
`ReceivedDataStream` is the one ingress subscription point and retains source,
target, endpoint, route, and arrival-time metadata alongside bytes.
`OutgoingRouter` resolves `{ targetId, route }` to active runtime bindings.
Bindings are replaced atomically only after every enabled binding validates with
its registered transport sender. A binding has `single`, `fanout`, or
`fallback` policy; commands default to `single` and never silently broadcast.

The BLE bridge is one sender and ingress binding implementation. Its endpoint
configuration uses a service UUID plus characteristic UUID and is revalidated
against the latest GATT discovery. Future Wi-Fi, MQTT, and LoRa implementations
register their own transport sender while the VM/protocol layer continues to
send only a logical route such as `runit.interface`.

## Packet packing

`src/backend/packetPack` serializes manually supplied or generated packet
schemas into exact packed little-endian bytes. It is deliberately not a C
parser, schema generator, transport, or router. A packet schema supplies its
class/function header bytes and an ordered field layout; `packPacket()` emits
the two header bytes followed by the packed payload.

Supported scalar aliases cover the explicit fixed-width C types used on the
wire: signed/unsigned 8/16/32/64-bit integers, `float`/`double`, `bool`, and
8/16/32/64-bit bitmasks. It also supports nested packed structs, fixed scalar
arrays, bytes, and UTF-8 ASCII fields. C bitfield layout is compiler-dependent
and is intentionally unsupported; encode it through an explicit bitmask field.

## Firmware IDs come from the generated JSON

The app hard-codes no firmware ID. Stream bytes, BLE UUIDs, class and packet
bytes, the response status values, error tags, owners and severities are all
read from `data-structures/` through `src/domain/descriptors/runitDescriptors.ts`
(imported with the `@data-structures` alias):

| File | Gives the app | Generated from |
|---|---|---|
| `contracts/contracts.generated.json`, `settings/settings.generated.json` | command catalog, response envelope | decoder headers |
| `enums.json` | enum values and names (e.g. the response `status_enum`) | `//#ref-enum` enums |
| `streams/streams.generated.json` | the stream byte of every board connector, and the BLE service / characteristics each one uses | `sys_data_connector_init()` table, `runit_board_defs.h` `//@STATIC_*` UUIDs, `runit_board_connector_bindings_init()` |
| `errors/errors.generated.json` | error tags, owners, severities, payload layouts with naming annotations, message templates, the schema ID, esp_err_t names | the sys_errors X-macro maps (`codes/sys_error_codes.h`), ESP-IDF `esp_err_to_name.c` |
| `board/board.generated.json` | onboard device IDs → names (`INA3221`, `TPS55289_0`) and their descriptors | `//@STATIC_DEVICE` in `runit_board_defs.h`, `RUNIT_BOARD_DEVICE` in `runit_board_cfg.c` |
| `vm/blocks/index.generated.json` | VM block type → title | `//#vm-block` |

The loaders cross-check the files against each other: the response stream in
the contracts must be the `interface` connector's byte, and the error packet's
stream must be the `errors` connector's byte. A mismatch throws at startup
instead of mis-routing frames. After changing any of those C sources,
regenerate the JSON (`data-structures/AGENTS.md`).

The backend (`src/backend`) takes these values as parameters
(`InterfaceProtocol`, `RunitBleLayout`) and never imports `data-structures/`.

## Commands and responses

`src/backend/protocol` implements the firmware's command envelope
(`components/system/sys_interface/SYS_INTERFACE.MD`, *Responses*):

```
app → board   [seq][class][packet][payload]                 (interface stream: its BLE write characteristic)
board → app   [stream][seq][class][packet][status][data]    (interface stream byte, its BLE notify characteristic)
```

- `CommandClient` owns the sequence byte. It counts on from the last one used,
  skipping bytes still in flight, so a late answer doesn't match a new command.
  An answer is matched by `seq` and must echo the command's class and packet;
  anything else is reported as `unmatched`.
- `send()` resolves with every answer: `ok` true, or `ok` false with
  `error: { tag, owner }`. `call()` rejects an ERROR answer with a
  `CommandError` whose code is `device-error`. Both reject with a `CommandError`
  on `timeout` (default 2 s from the end of the write), `send-failed` or
  `cancelled` (after `close()`, or when the board disconnects).
- By default one command is in flight at a time (`maxInFlight: 1`). The board
  answers in order anyway, and a slow command can't push the ones queued behind
  it past their timeouts. Raise the limit only after checking how deep the
  firmware's receive queue is.
- `observe()` reports `sent` / `response` / `timeout` / `unmatched` events for
  consoles and logs.
- `openRunitBleSession(adapter, { layout, protocol })`
  (`src/backend/runitBleSession.ts`) runs after connect and `discover()`. It
  writes commands to `layout.commandWrite`, subscribes to every
  `layout.notify` characteristic into one `ReceivedDataStream`, and closes
  itself on disconnect.
- The Web Bluetooth adapter runs its GATT operations one after another. Chromium
  rejects an operation that overlaps another.

The command catalog fails to build if two commands share a class/packet pair.
`packCommand()` always sends every field: an optional field left out is sent as
its sentinel or 0 (features.md F-GAP-10). `decodeResponseData()` reads an OK
answer's data using the command's `response` layout. An ERROR answer's tag and
owner are shown by name from the error catalog.

## Errors and logs

`src/domain/decoder` turns board frames into typed events.
`decodeBoardFrame()` looks the first byte up in the stream catalog. It decodes
`logs` and `errors` frames and hands the other streams back untouched.

**Errors** (`errorPacket.ts`): `[stream][u8 node_count][u8 depth][u32 schema_id]`,
then per node `[u8 payload_length][u16 tag][u16 owner][payload]`, outermost
error first and root cause last (`SYS_ERRORS.MD`, "Logging and telemetry").

- Every node gets its tag, owner and default severity from the catalog.
  - Its payload fields are read at their native C offsets.
  - Its message is rendered from the firmware's own `LOG_BODY` format string by
    `formatPrintf()`, a small C printf.
  - An argument the firmware passes through a value → text helper (for example
    `vm_run_mode_name`) is printed from the published `value_names` table.
    `esp_err_to_name` and `vm_format_obj_id` aren't tables, so their values
    print as numbers.
- **Named IDs.** A payload field annotated in the firmware map
  (`/*@enum-ref <enum>*/`, `/*@id <kind>*/`) is named by
  `runitValueNames()` (`domain/descriptors/valueNames.ts`) from the matching
  JSON: an enum member's alias, a board device, an error tag / owner / level, a
  command class or packet (the packet reads its class from a sibling field),
  a device contract feature (reads its contract type from a sibling field),
  a VM block type, an esp_err_t name. The node's `labels` hold the names; the
  message prints them after the number (`device 12 (INA3221) is not
  registered`); an argument the firmware passes through a helper prints the
  helper's text, or the name when the helper isn't a published table
  (`ESP-IDF error ESP_ERR_NO_MEM (0x101)`). An ID no catalog knows (a device
  added at run time) stays a number. The loader refuses an errors JSON that
  uses an `@id` kind the app can't name yet.
- A tag with no message template (6 of 120, whose descriptions use
  conditionals) is shown as `field=value` pairs.
- An ID the catalog doesn't know is shown in hex.
- `schemaMatches` compares the packet's schema ID with the catalog's. The
  generator computes `SE_schema_id()` the way `sys_error.c` does. A mismatch
  means the JSON is older or newer than the flashed firmware: known tags still
  decode, because tag IDs are stable.
- `truncated` (the chain was cut to fit the frame), `corrupt` (depth 255) and
  `malformed` (the frame ended inside a node) are reported, never thrown.

**Logs** (`logLines.ts`): each frame is UTF-8 text, one ESP-IDF log line per
frame (`L (ms) tag: text`), or the text version of an error chain
(`[depth] owner=NAME (0xOOOO) tag=NAME (id): description`), which duplicates
the binary errors stream. Colour escapes are stripped. Anything else is kept
as plain text.

The test console shows both. The Errors panel collapses each chain to its root
cause and says whether the board's schema ID matches. The Logs panel filters
by level and text, and hides the error-chain lines by default.

Tests: `npm test` (Vitest). They cover:
- the packer, on the frames documented in SYS_INTERFACE.MD;
- sentinel filling and response decoding;
- the client's seq matching, timeout, pipelining, wrap-around, send failure and close;
- the catalog cross-checks;
- C printf formatting;
- error packets, built from the real catalog: messages, value tables, chains, truncation, mismatches, unknown tags, cut frames;
- log line parsing;
- stream routing.
