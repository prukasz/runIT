# SYS_INTERFACE — Inbound Packet Router

`sys_interface` is the single entry point for every inbound control frame, regardless of transport. It owns a **two-level dispatch**: a class byte selects a decoder table, and that table's own packet byte selects the action.

The decoder tables themselves live in the `codecs` component (`components/codecs/decoders/`) — see [[CODECS.MD]]. `sys_interface` never knows what a packet *means*; it only knows which table owns it.

---

## Wire Format

```
[0xXX] [0xYY] [ payload ... ]
 class  packet
```

| Field | Meaning |
| :--- | :--- |
| `0xXX` | **Class byte** — selects the registered decoder table (`sys_interface_register_decoder()`). |
| `0xYY` | **Packet byte** — interpreted by that class handler alone. Two classes may reuse the same packet byte for unrelated packets. |
| payload | The class/packet's `__packed` payload struct, copied verbatim. |

`sys_interface_decode()` consumes `0xXX` and forwards the remainder, so a class handler always sees `0xYY` at `data[0]`.

### Registered Classes

| Class | Owner | Handler |
| :--- | :--- | :--- |
| `0x01` | System contracts (`sys_device` / `sys_io` / `sys_power`) | `dec_sys_contracts_decode()` — registered by `sys_interface_init()` |
| `0x03` | System actions (record/stop/remove) | `dec_sys_actions_decode()` — registered by `sys_actions_init()`, see [[SYS_ACTIONS.MD]] |
| `0x04` | VM program upload (reset/open/add objects/set data) | `dec_vm_loader_decode()` — registered by `sys_interface_init()`, see [[VM.MD]] |
| `0x02`, `0x05`+ | Free — dynamic device/callback creation, etc. | register your own |

Up to `SYS_INTERFACE_MAX_CLASSES` (8) classes can be bound at once. The registry is a flat array with linear search — it is not mutex-protected, so register all classes during boot rather than from concurrent tasks.

---

## Data Flow

```
Transport (e.g. BLE peer write, UART, etc.)
   -> linked to sys_data_connector (CONN_ID_INTERFACE)
   -> provider signals conn->data_present semaphore
   -> sys_interface_receiver_task   (wakes on conn->data_present, or every SYS_INTERFACE_RX_WAIT_MS)
   -> [suspended? -> leave queued in transport, don't drain]
   -> sys_data_connector_receive()
   -> [pushed into the static tap buffer when capture is active]
   -> sys_interface_decode(frame, len) (strips class byte 0xXX)
   -> class handler                    (e.g. dec_sys_contracts_decode, dec_vm_loader_decode)
   -> convert_to_packet()              (length-checked memcpy into the payload struct)
   -> decoder_packet_xxx_t()           (calls the sys_* contract API)
```

The single receiver task blocks on the interface connector's `data_present` semaphore with a `SYS_INTERFACE_RX_WAIT_MS` (100ms) timeout. Bound transport providers wake the receiver instantly upon frame reception. On every wake, the task pulls frames via `sys_data_connector_receive()` until empty.

Errors from a decoder are emitted with `SE_ORIGIN_CALL()` inside the receiver task, so a malformed or failing frame is reported through the normal error telemetry path and the receiver keeps running.

---

## Public API

### `err_h sys_interface_init(void)`
Clears the class registry, initializes the interface data connector (`CONN_ID_INTERFACE`), binds built-in classes (`0x01` system contracts, `0x04` VM loader), and starts `sys_interface_receiver_task`.

### `err_h sys_interface_send(const void* data, size_t len)`
Dispatches outbound data over the virtual interface connector (`CONN_ID_INTERFACE`) to all bound TX providers via `sys_data_connector_send()`.

### `err_h sys_interface_register_decoder(uint8_t decoder_header, sys_interface_handler_f decoder, const char* name)`
Registers a decoder table for a class byte. `name` is used in logs only and may be `NULL`. Decoders are only ever appended (`s_decoders[0..s_decoder_count-1]`) — there is no unregister. Register classes during boot before processing frames.

* `ERR_INTERFACE_CLASS_TAKEN` — that class byte is already bound.
* `ERR_INTERFACE_NO_CLASS_SLOTS` — all `SYS_INTERFACE_MAX_CLASSES` slots are in use.

### `err_h sys_interface_decode(const uint8_t* data, size_t len)`
Routes one complete frame. Returns `ERR_INTERFACE_SHORT_FRAME` on an empty frame, `ERR_INTERFACE_UNKNOWN_CLASS` for an unregistered class byte, or the handler's own error chain. Called from the RX receiver task for live traffic, and directly by `sys_actions_invoke()` for replay.

### `void sys_interface_tap_capture_start(void)` / `void sys_interface_tap_capture_end(void)` / `err_h sys_interface_tap_poll(uint8_t* buf, size_t max_len, size_t* out_len)`
A pull-based frame observer backed by a statically allocated `sys_buff_t` ring buffer of `CONFIG_SYS_ACTIONS_TAP_BUFFER_SIZE` bytes. Capture is off by default. While enabled, live frames are copied into the tap ring buffer before dispatch.

### `void sys_interface_suspend_rx(void)` / `void sys_interface_resume_rx(void)` / `bool sys_interface_is_rx_suspended(void)`
Nesting-safe suspend of the RX receiver's draining. While suspended, the receiver task does not drain frames from the connector. If a wake signal was received during suspension, it is preserved so resume notices the pending data immediately.

### `err_h convert_to_packet(const uint8_t* data, size_t len, void* packet, size_t packet_size)`
Length-checked copy of raw bytes into a `__packed` payload struct. Returns `ERR_INTERFACE_SHORT_FRAME` if the frame is too short.

### `typedef err_h (*sys_interface_handler_f)(const uint8_t* data, size_t len)`
Class handler signature. `data[0]` is the packet byte — the class byte is already stripped.

---

## Usage

```c
// Boot: initialize interface router and connectors
CHECK_AND_LOG(sys_interface_init());
```

Adding a new class (`0x02` and `0x05`+ are free — the VM took `0x04`, see [[VM.MD]]):

```c
// dec_foo.h — same header-only shape as dec_sys_contracts.h
#define FOO_CLASS_HEADER 0x02
static inline err_h dec_foo_decode(const uint8_t* data, size_t len) { ... }

// at boot, after sys_interface_init()
CHECK_AND_LOG(sys_interface_register_decoder(FOO_CLASS_HEADER, dec_foo_decode, "foo"));
```

Example frames:

| Bytes | Meaning |
| :--- | :--- |
| `01 24 03 04` | class `0x01`, packet `0x24` (`sys_io_toggle`), device 3, pin 4 |
| `01 22 00 05 01` | class `0x01`, packet `0x22` (`sys_io_set_level`), device 0, pin 5, level high |
| `01 28 03 04 E8 03 00 00` | class `0x01`, packet `0x28` (`sys_io_set_pwm_duty`), device 3, pin 4, duty 1000 (LE `uint32`) |

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

| Owner | ID |
| :--- | :--- |
| `OWNER_SYS_INTERFACE_BASE` | `0xA600` |
| `OWNER_SYS_INTERFACE_DECODE` | `0xA601` |
| `OWNER_SYS_INTERFACE_CLASS` | `0xA602` |
| `OWNER_SYS_INTERFACE_SOURCE` | `0xA603` |
| `OWNER_DEC_SYS_CONTRACTS` | `0xA610` |
| `OWNER_DEC_SYS_ACTIONS` | `0xA611` |
| `OWNER_DEC_SYS_DEVICE_INSTALL` | `0xA612` |
| `OWNER_ENC_SYS_ERRORS` | `0xA620` |

Codec owners live here (rather than in `codecs`, which is header-only and has no error map of its own): `0xA61x` for decoders, `0xA62x` for encoders.
