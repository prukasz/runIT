# SYS_INTERFACE — Inbound Packet Router

`sys_interface` is the single entry point for every inbound control frame, regardless of transport. It owns a **two-level dispatch**: a class byte selects a decoder table, and that table's own packet byte selects the action.

The decoder tables themselves live in the `codecs` component (`components/codecs/decoders/`) — see [[CODECS.MD]]. `sys_interface` never knows what a packet *means*; it only knows which table owns it.

---

## Wire Format

```
[seq] [0xXX] [0xYY] [ payload ... ]
       class  packet
```

| Field | Meaning |
| :--- | :--- |
| `seq` | **Sequence byte** — the client's own counter (any value, wraps at 256), echoed in the response so the client matches the answer to its command. Only live commands from a client carry it: the RX task removes it before recording (tap) or decoding, so decoders, recorded actions and `sys_interface_decode()` work on `[0xXX][0xYY][payload]`. |
| `0xXX` | **Class byte** — selects the registered decoder table (`sys_interface_register_decoder()`). |
| `0xYY` | **Packet byte** — interpreted by that class handler alone. Two classes may reuse the same packet byte for unrelated packets. |
| payload | The class/packet's `__packed` payload struct, copied verbatim. |

`sys_interface_decode()` consumes `0xXX` and forwards the remainder, so a class handler always sees `0xYY` at `data[0]`.

### Registered Classes

| Class | Owner | Handler |
| :--- | :--- | :--- |
| `0x01` | System contracts (`sys_device` / `sys_io` / `sys_power`) | `dec_sys_contracts_decode()` |
| `0x02` | BLE settings | `dec_settings_ble_decode()` |
| `0x03` | System actions (record/stop/remove) | `dec_sys_actions_decode()`, see [[SYS_ACTIONS.MD]] |
| `0x04` | VM program upload (reset/open/add objects/set data) | `dec_vm_loader_decode()`, see [[VM.MD]] |
| `0x05` | Features (servo, H-bridge) | `dec_features_decode()` |
| `0x06` | Data connector settings | `dec_settings_data_connector_decode()` |
| `0x07` | Log settings | `dec_settings_logs_decode()` |
| `0x08` | Power settings (persisted) | `dec_settings_power_decode()`, see [[SYS_POWER.MD]] |
| `0x09` | Event subscriptions | `dec_sys_events_decode()`, see [[SYS_EVENT.MD]] |
| `0x0A`+ | Free | register your own |

Class bytes are Kconfig options (`CONFIG_RX_PACKET_CLASS_*`). `sys_interface` registers no classes and doesn't depend on `codecs`: all of them are registered by runit's `runit_register_decoders` boot step (`components/runit/runit_decoders.c`), before `sys_interface_init()` starts the RX receiver.

Up to `CONFIG_SYS_INTERFACE_MAX_CLASSES` (12) classes can be bound at once. The registry is a flat array with linear search — it is not mutex-protected, so register all classes during boot rather than from concurrent tasks.

---

## Data Flow

```
Transport provider (BLE write, UART, Wi-Fi, ...) queues one whole frame
   -> sys_data_connector_notify_rx(SYS_DATA_CONNECTOR_INTERFACE)
   -> sys_interface_receiver_task   (sys_data_connector_wait_rx(), or every SYS_INTERFACE_RX_WAIT_MS)
   -> [suspended? -> leave queued in the provider, sleep until resume notifies]
   -> sys_data_connector_receive()     (frame + its origin: provider ID and peer)
   -> seq byte removed and kept for the response
   -> [pushed into the static tap buffer when capture is active, without seq]
   -> decode_live_frame(seq, frame, len, origin)  (response capture on; strips class byte 0xXX)
   -> class handler                    (e.g. dec_sys_contracts_decode, dec_vm_loader_decode)
   -> convert_to_packet()              (length-checked memcpy into the payload struct)
   -> decoder_packet_xxx_t()           (calls the sys_* contract API; a getter calls sys_interface_respond())
   -> response [seq][class][packet][status][data] via sys_data_connector_send_to(origin)
   -> error chain (if any) to SE_REPORT()
```

`sys_interface` is transport-agnostic: it knows only the interface connector's ID. Which transports feed it, and how they frame bytes, is the providers' business ([[SYS_DATA_CONNECTOR.MD]]).

The single receiver task drains the connector, then waits in `sys_data_connector_wait_rx()` with a `SYS_INTERFACE_RX_WAIT_MS` (100ms) timeout; providers wake it as soon as they queue a frame. Frames are pulled round-robin across the RX providers until none is left. Inbound frames are at most `CONFIG_SYS_DATA_CONNECTOR_FRAME_MAX` bytes; a longer one is dropped by the provider and reported, never decoded cut short.

Errors from a decoder are emitted with `SE_REPORT()` inside the receiver task, so a malformed or failing frame is reported through the normal error telemetry path and the receiver keeps running.

## Responses

Every live command frame is answered on the interface connector (`SYS_DATA_CONNECTOR_INTERFACE`, whose framing byte is `CONFIG_TX_PACKET_CLASS_INTERFACE` = `0x05`):

```
[0x05] [seq] [class] [packet] [status] [data ...]
```

* **`seq` / `class` / `packet`:** echo the request's first three bytes; a byte the frame lacks is `0x00` (a frame that is only `seq` is answered with `ERR_INTERFACE_SHORT_FRAME`).
* **Matching:** the client matches a response to its command by `seq`, and gives up on a command after a timeout. A lost response then fails only that command instead of shifting every later answer. Commands are still answered one at a time, in the order received, exactly once each.
* **Mixed streams:** only stream `0x05` carries `seq`. Telemetry, logs, errors and events are unsolicited pushes on their own stream bytes and may arrive between a command and its response.
* **Destination:** only the origin of the command (its provider's TX binding on the interface connector, addressed to its peer). Each client numbers its own commands; clients on different transports or sessions never see each other's answers, so equal `seq` values can't collide.
* **Too long for the link:** if the origin's transport can't carry the answer right now (a getter response over a 20-byte BLE link before the MTU exchange), the command is answered with error status and `ERR_DATA_CONNECTOR_FRAME_TOO_LONG` instead, so it still gets exactly one response.
* **`status`** (`sys_interface_status_e`):
  * `0` OK: `data` is the command's response struct `packet_<name>_response_t` (packed, little-endian), or empty.
  * `1` error: `data` is `u16 tag, u16 owner` of the error chain's root cause. The full chain goes to the errors stream as before, and any data the decoder had attached is dropped.
* **Getters** build their response struct and call **`sys_interface_respond(data, len)`**. The response data capacity is `CONFIG_SYS_INTERFACE_RESPONSE_MAX` (default 128 B); more gives `ERR_INTERFACE_RESPONSE_TOO_LONG`.
* **Replayed frames aren't answered.** `sys_interface_decode()`, which `sys_actions` uses to replay recorded frames, runs with response capture off, including when the replay is nested inside a live frame. So a replay never answers or corrupts the outer frame's response. The capture pointer is task-local and set only by the RX task.
* **Layouts for the app:** each contract with a response struct gets a `response` layout in `data-structures/contracts/contracts.generated.json`, and the envelope is its `response_stream`.

---

## Public API

### `err_h sys_interface_init(void)`
Checks that the interface data connector (`SYS_DATA_CONNECTOR_INTERFACE`) exists (`ERR_BASE_NOT_FOUND` otherwise), sets up the tap buffer and starts `sys_interface_receiver_task`. Registers no classes: register them before this call, so no frame arrives while the class table is being filled.

### `err_h sys_interface_register_decoder(uint8_t decoder_header, sys_interface_handler_f decoder, const char* name)`
Registers a decoder table for a class byte. `name` is used in logs only and may be `NULL`. Decoders are only ever appended (`s_decoders[0..s_decoder_count-1]`) — there is no unregister. Register classes during boot, before `sys_interface_init()`.

* `ERR_INTERFACE_CLASS_TAKEN` — that class byte is already bound.
* `ERR_INTERFACE_NO_CLASS_SLOTS` — all `SYS_INTERFACE_MAX_CLASSES` slots are in use.

### `err_h sys_interface_decode(const uint8_t* data, size_t len)`
Routes one complete frame. Returns `ERR_INTERFACE_SHORT_FRAME` on an empty frame, `ERR_INTERFACE_UNKNOWN_CLASS` for an unregistered class byte, or the handler's own error chain. Used for replay (`sys_actions_invoke()`) and direct calls, so it doesn't answer the frame. Live frames are decoded and answered by the RX task (see Responses).

### `err_h sys_interface_respond(const void* data, size_t len)`
Attaches response data to the live command being decoded (see Responses). It does nothing outside a live frame. Returns `ERR_INTERFACE_RESPONSE_TOO_LONG` above `CONFIG_SYS_INTERFACE_RESPONSE_MAX`.

### `void sys_interface_tap_capture_start(void)` / `void sys_interface_tap_capture_end(void)` / `err_h sys_interface_tap_poll(uint8_t* buf, size_t max_len, size_t* out_len)`
A pull-based frame observer backed by a statically allocated `sys_buff_t` ring buffer of `CONFIG_SYS_ACTIONS_TAP_BUFFER_SIZE` bytes. Capture is off by default. While enabled, live frames are copied into the tap ring buffer before dispatch.

### `void sys_interface_suspend_rx(void)` / `void sys_interface_resume_rx(void)` / `bool sys_interface_is_rx_suspended(void)`
Nesting-safe suspend of the RX receiver's draining. While suspended, the receiver task does not drain frames from the connector (they wait in the providers) and sleeps; the last resume notifies it, so pending frames are drained right away. A frame that suspends RX while being decoded (the fault hook) stops the drain after that frame.

### `err_h convert_to_packet(const uint8_t* data, size_t len, void* packet, size_t packet_size)`
Length-checked copy of raw bytes into a `__packed` payload struct. Returns `ERR_INTERFACE_SHORT_FRAME` if the frame is too short.

### `typedef err_h (*sys_interface_handler_f)(const uint8_t* data, size_t len)`
Class handler signature. `data[0]` is the packet byte — the class byte is already stripped.

---

## Usage

Adding a new class (`0x0A`+ are free): add `CONFIG_RX_PACKET_CLASS_FOO` to `components/utils/Kconfig`, then

```c
// dec_foo.h — same header-only shape as dec_sys_contracts.h
static inline SE_MUST_USE err_h dec_foo_decode(const uint8_t* data, size_t len) { ... }

// at boot, in runit_register_decoders() - before sys_interface_init()
SE_TRY(sys_interface_register_decoder(CONFIG_RX_PACKET_CLASS_FOO, dec_foo_decode, "foo"));
```

Example frames, as a client sends them (first byte = `seq`):

| Bytes | Meaning |
| :--- | :--- |
| `07 01 24 03 04` | seq 7, class `0x01`, packet `0x24` (`sys_io_toggle`), device 3, pin 4 |
| `08 01 22 00 05 01` | seq 8, class `0x01`, packet `0x22` (`sys_io_set_level`), device 0, pin 5, level high |
| `09 01 28 03 04 E8 03 00 00` | seq 9, class `0x01`, packet `0x28` (`sys_io_set_pwm_duty`), device 3, pin 4, duty 1000 (LE `uint32`) |

The answer to the first one is `05 07 01 24 00` (stream `0x05`, seq 7, class, packet, status OK). Recorded actions store and replay the frames without `seq` (`01 24 03 04`).

Multi-byte payload fields are little-endian and **not** naturally aligned — every payload struct is `__packed`, so the sender must pack fields back-to-back with no padding.

---

## Errors & Owners

| Tag | Payload | Raised when |
| :--- | :--- | :--- |
| `ERR_INTERFACE_SHORT_FRAME` | `uint32_t got; uint32_t need;` | Frame shorter than the header or the payload struct. |
| `ERR_INTERFACE_UNKNOWN_CLASS` | `uint8_t class_header;` | No handler registered for `data[0]`. |
| `ERR_INTERFACE_UNKNOWN_PACKET` | `uint8_t class_header; uint8_t packet_header;` | Class matched, packet byte unmapped in its table. |
| `ERR_INTERFACE_CLASS_TAKEN` | `uint8_t class_header;` | Class byte already bound. |
| `ERR_INTERFACE_NO_CLASS_SLOTS` | `uint8_t class_header;` | Class registry full. |
| `ERR_INTERFACE_ENC_BUF_TOO_SMALL` | `uint32_t got; uint32_t need;` | An encoder's destination buffer cannot hold even one record. |
| `ERR_INTERFACE_RESPONSE_TOO_LONG` | `uint32_t got; uint32_t max;` | Response data above `CONFIG_SYS_INTERFACE_RESPONSE_MAX`. |

Owners (`errors/sys_error_interface.h`): `OWNER_SYS_INTERFACE_*` (`0xA600`–`0xA604`), one `OWNER_DEC_*` per decoder file (`0xA610`–`0xA618`), `OWNER_ENC_SYS_ERRORS` (`0xA620`).

Codec owners live here (rather than in `codecs`, which is header-only and has no error map of its own): `0xA61x` for decoders, `0xA62x` for encoders.
