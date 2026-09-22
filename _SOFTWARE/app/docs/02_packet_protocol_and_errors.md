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

The runIT board currently exposes RX `0xFFE2` for writes and TX `0xFFE1`,
status, and logs `0xFFE3`-adjacent characteristics for notifications.  The
backend discovers instead of hard-coding that topology; a higher protocol
layer owns resolving the named runIT endpoints.

## Desktop test console

The temporary `BleTestApp.tsx` is a manual integration console for the Web
Bluetooth adapter. It selects a runIT-advertising device, connects,
rediscovers its GATT database, reads or subscribes to individual
characteristics, and writes explicit hexadecimal bytes. It intentionally does
not assemble protocol packets, retain device state, or implement VM/device
screens. Those belong above this raw transport boundary.

Web Bluetooth does not expose GATT descriptor enumeration or direct descriptor
reads/writes. Discovery therefore adds an **inferred** Client Characteristic
Configuration Descriptor (CCCD, `0x2902`) to every notify/indicate
characteristic. `Subscribe` and `Unsubscribe` remain the only portable way to
change its value. Native adapters may replace inferred entries with enumerated
descriptor metadata later.

The raw BLE adapter deliberately returns platform-provided UUIDs only. The
test app applies a separate runIT GATT catalog for its known service and
characteristics: service `0xFFE0`, TX `0xFFE1`, RX `0xFFE2`, logs `0xFFE3`,
and status `0xFFE4`. This preserves a reusable adapter while giving the test
operator meaningful names and both 16-bit and full UUID forms.

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
