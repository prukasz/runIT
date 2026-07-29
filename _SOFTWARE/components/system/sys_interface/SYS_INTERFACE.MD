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
| `0xXX` | **Class byte** — selects the registered decoder table (`sys_interface_register_class()`). |
| `0xYY` | **Packet byte** — interpreted by that class handler alone. Two classes may reuse the same packet byte for unrelated packets. |
| payload | The class/packet's `__packed` payload struct, copied verbatim. |

`sys_interface_decode()` consumes `0xXX` and forwards the remainder, so a class handler always sees `0xYY` at `data[0]`.

### Registered Classes

| Class | Owner | Handler |
| :--- | :--- | :--- |
| `0x01` | System contracts (`sys_device` / `sys_io` / `sys_power`) | `dec_sys_contracts_decode()` — registered by `sys_interface_init()` |
| `0x03` | System actions (record/stop/remove) | `dec_sys_actions_decode()` — registered by `sys_actions_init()`, see [[SYS_ACTIONS.MD]] |
| `0x02`, `0x04`+ | Free — VM bytecode, dynamic device/callback creation, etc. | register your own |

Up to `SYS_INTERFACE_MAX_CLASSES` (8) classes can be bound at once. The registry is a flat array with linear search — it is not mutex-protected, so register all classes during boot rather than from concurrent tasks.

---

## Data Flow

```
BLE peer write
   -> characteristic rx_buff (sys_buff_t, one write = one whole item)
   -> rx_sem given
   -> sys_if_rx worker task            (sys_interface_bind_ble_rx)
   -> [dropped if sys_interface_is_rx_suspended()]
   -> sys_interface_decode(frame, len) (strips class byte 0xXX)
   -> [tap, if set, sees the full frame here - non-invasive, does not affect the below]
   -> class handler                    (e.g. dec_sys_contracts_decode)
   -> convert_to_packet()              (length-checked memcpy into the payload struct)
   -> decoder_packet_xxx_t()           (calls the sys_* contract API)
```

Because `sys_buff_t` is a `RINGBUF_TYPE_NOSPLIT` buffer, one peer write is always one whole frame — the router never has to reassemble or re-frame anything.

Errors from a decoder are emitted with `SE_ORIGIN_CALL()` inside the worker task, so a malformed or failing frame is reported through the normal error telemetry path and the pump keeps running.

---

## Public API

### `err_h sys_interface_init(void)`
Clears the class registry and binds class `0x01` to the system contracts table. Call once at boot, before any other `sys_interface` call.

### `err_h sys_interface_register_class(uint8_t class_header, sys_interface_handler_f handler, const char* name)`
Binds a decoder table to a class byte. `name` is used in logs only and may be `NULL`. Classes are only ever appended (`s_classes[0..s_class_count-1]`, a plain incrementing count, no free-slot search) — there is no unregister. Register everything at boot, before `sys_interface_bind_ble_rx()` starts the RX pump.

* `ERR_INTERFACE_CLASS_TAKEN` — that class byte is already bound.
* `ERR_INTERFACE_NO_CLASS_SLOTS` — all `SYS_INTERFACE_MAX_CLASSES` slots are in use.

### `err_h sys_interface_decode(const uint8_t* data, size_t len)`
Routes one complete frame. Returns `ERR_INTERFACE_SHORT_FRAME` on an empty frame, `ERR_INTERFACE_UNKNOWN_CLASS` for an unregistered class byte, or the handler's own error chain.

### `err_h sys_interface_bind_ble_rx(uint16_t char_uuid, size_t max_frame_len)`
Attaches a BLE characteristic's RX buffer as the frame source: starts the single static `sys_if_rx` worker task (`R_TASK_DEFINE`/`R_TASK_START`, no heap allocation) that blocks on the characteristic's `rx_sem`, drains every queued write into a static frame buffer, and feeds each to `sys_interface_decode()`. There is only one RX pump — `max_frame_len` must be `<= SYS_INTERFACE_RX_FRAME_CAP` (512).

Fails with `ERR_INVALID_VAL_UI32` if `max_frame_len` is 0 or exceeds the cap, `ERR_BASE_INVALID_STATE` if already bound or the characteristic exists but was created with `rx_buffer_size == 0`, or `ERR_BASE_NO_MEM` if the task fails to start. The binding is permanent — there is no unbind.

### `void sys_interface_set_tap(sys_interface_tap_f tap)`
Registers (or, with `NULL`, clears) a single non-invasive observer called with every complete frame `sys_interface_decode()` receives — class byte included, before dispatch. It cannot affect decode's return value or control flow. Only one tap can be registered at a time; a later call replaces the previous one. This is what [[SYS_ACTIONS.MD]] uses to record live traffic without sys_interface knowing anything about actions.

```c
typedef void (*sys_interface_tap_f)(const uint8_t* frame, size_t len);
```

### `void sys_interface_suspend_rx(void)` / `void sys_interface_resume_rx(void)` / `bool sys_interface_is_rx_suspended(void)`
Nesting-safe suspend of the RX pump's dispatch, same depth-counter shape as `SE_suspend()`/`SE_resume()` in [[SYS_ERRORS.MD]]. This is a **best-effort signal, not a hard barrier**: while suspended, `sys_interface_bind_ble_rx()`'s worker task still drains each characteristic's RX buffer (so it can't overflow) but drops every frame instead of decoding it, logging a warning per drop — it does not wait for an in-flight `sys_interface_decode()` call to finish. Used by `sys_actions_invoke()` so a replayed packet sequence can't interleave with live incoming traffic.

### `err_h convert_to_packet(const uint8_t* data, size_t len, void* packet, size_t packet_size)`
Length-checked copy of raw bytes into a `__packed` payload struct. Returns `ERR_INTERFACE_SHORT_FRAME` (carrying `got`/`need`) if the frame is too short. Used by the `X`-macro-generated decode cases; rarely called directly.

### `typedef err_h (*sys_interface_handler_f)(const uint8_t* data, size_t len)`
Class handler signature. `data[0]` is the packet byte — the class byte is already stripped.

---

## Usage

```c
// Boot: init the router, then attach the BLE RX characteristic as a source.
CHECK_AND_LOG(sys_interface_init());
CHECK_AND_LOG(sys_interface_bind_ble_rx(SYS_BLE_CHR_RUNIT_RX, RUNIT_BLE_RX_FRAME_MAX));
```

Adding a new class (e.g. the VM):

```c
// dec_vm.h — same header-only shape as dec_sys_contracts.h
#define VM_CLASS_HEADER 0x02
static inline err_h dec_vm_decode(const uint8_t* data, size_t len) { ... }

// at boot, after sys_interface_init()
CHECK_AND_LOG(sys_interface_register_class(VM_CLASS_HEADER, dec_vm_decode, "vm"));
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
| `ERR_INTERFACE_NO_CLASS_SLOTS` | `uint8_t class_header;` | Registry full. |
| `ERR_INTERFACE_ENC_BUF_TOO_SMALL` | `uint32_t got; uint32_t need;` | An encoder's destination buffer cannot hold even one record. |

| Owner | ID |
| :--- | :--- |
| `OWNER_SYS_INTERFACE_BASE` | `0xA600` |
| `OWNER_SYS_INTERFACE_DECODE` | `0xA601` |
| `OWNER_SYS_INTERFACE_CLASS` | `0xA602` |
| `OWNER_SYS_INTERFACE_SOURCE` | `0xA603` |
| `OWNER_DEC_SYS_CONTRACTS` | `0xA610` |
| `OWNER_DEC_SYS_ACTIONS` | `0xA611` |
| `OWNER_ENC_SYS_ERRORS` | `0xA620` |

Codec owners live here (rather than in `codecs`, which is header-only and has no error map of its own): `0xA61x` for decoders, `0xA62x` for encoders.
