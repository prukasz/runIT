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
BLE peer write (or any other registered source)
   -> characteristic rx_buff (sys_buff_t, one write = one whole item)
   -> producer gives sys_interface_get_rx_wake_sem(), if it has one (e.g. rx_notify_sem)
   -> sys_interface_receiver_task   (single shared task - wakes on the semaphore, or every SYS_INTERFACE_RX_WAIT_MS)
   -> [suspended? -> leave everything queued in each source's own buffer, don't drain - see suspend/resume below]
   -> source's dequeue_fn(ctx, ...) (sys_interface_register_rx_source / sys_interface_bind_ble_rx)
   -> [pushed into the tap buffer too, if enabled - see sys_interface_tap_enable()]
   -> sys_interface_decode(frame, len) (strips class byte 0xXX)
   -> class handler                    (e.g. dec_sys_contracts_decode)
   -> convert_to_packet()              (length-checked memcpy into the payload struct)
   -> decoder_packet_xxx_t()           (calls the sys_* contract API)
```

Because `sys_buff_t` is a `RINGBUF_TYPE_NOSPLIT` buffer, one peer write is always one whole frame — the router never has to reassemble or re-frame anything.

There is exactly one receiver task no matter how many sources are registered. It blocks on `sys_interface_get_rx_wake_sem()` - a binary semaphore **owned by `sys_interface` itself**, not any one transport - with a `SYS_INTERFACE_RX_WAIT_MS` (100ms) timeout. Any producer that wants near-instant wake just gives that handle when it has data (e.g. pass it as `sys_ble_char_create_t.rx_notify_sem` at characteristic-creation time); a producer that never gives it is still caught every timeout tick. On every wake (early or timeout) the receiver drains **every** registered source with its `dequeue_fn` until that source reports nothing pending, then moves to the next - it never tries to figure out which producer actually signaled, so any number of producers can share the one handle with no per-source bit or identity to assign.

Errors from a decoder are emitted with `SE_ORIGIN_CALL()` inside the receiver task, so a malformed or failing frame is reported through the normal error telemetry path and the receiver keeps running.

---

## Public API

### `err_h sys_interface_init(void)`
Clears the class registry and binds class `0x01` to the system contracts table. Call once at boot, before any other `sys_interface` call.

### `err_h sys_interface_register_class(uint8_t class_header, sys_interface_handler_f handler, const char* name)`
Binds a decoder table to a class byte. `name` is used in logs only and may be `NULL`. Classes are only ever appended (`s_classes[0..s_class_count-1]`, a plain incrementing count, no free-slot search) — there is no unregister. Register everything at boot, before the first `sys_interface_register_rx_source()`/`sys_interface_bind_ble_rx()` call starts the RX receiver.

* `ERR_INTERFACE_CLASS_TAKEN` — that class byte is already bound.
* `ERR_INTERFACE_NO_CLASS_SLOTS` — all `SYS_INTERFACE_MAX_CLASSES` slots are in use.

### `err_h sys_interface_decode(const uint8_t* data, size_t len)`
Routes one complete frame. Returns `ERR_INTERFACE_SHORT_FRAME` on an empty frame, `ERR_INTERFACE_UNKNOWN_CLASS` for an unregistered class byte, or the handler's own error chain. Called from the RX receiver task for live traffic, and directly by `sys_actions_invoke()` for replay - the latter is never tapped (see `sys_interface_tap_enable()` below) and doesn't go through source draining/suspend at all.

### `SemaphoreHandle_t sys_interface_get_rx_wake_sem(void)`
Returns the shared RX-wake semaphore the receiver task blocks on. A binary semaphore, owned by `sys_interface` (not any transport) so any number of producers can share it - give it whenever you have data for a registered source and the receiver wakes near-instantly instead of waiting for its next `SYS_INTERFACE_RX_WAIT_MS` tick. Always valid (constructed at load time), so it's safe to read and pass around (e.g. into `sys_ble_char_create_t.rx_notify_sem`) before `sys_interface_init()` has even run.

### `err_h sys_interface_register_rx_source(sys_interface_rx_dequeue_f dequeue_fn, void* ctx, size_t max_frame_len, const char* name)`
Registers a frame source with the shared RX receiver — the generic mechanism `sys_interface_bind_ble_rx()` is a thin wrapper over. `dequeue_fn(ctx, buf, max_len, out_len)` must be non-blocking: pop at most one frame, or report `*out_len = 0` if nothing is pending. Sources are only ever appended (`s_rx_sources[0..s_rx_source_count-1]`, same plain-count shape as the class registry) — there is no unregister. Up to `SYS_INTERFACE_MAX_RX_SOURCES` (4) may be registered, and unlike classes, **registration is not boot-only** — call it any time after `sys_interface_init()`, including well after boot when a transport (WiFi, LoRa, ...) comes up.

The single static receiver task (`R_TASK_DEFINE`/`R_TASK_START`, no heap allocation) starts lazily on the first successful registration and is never duplicated — every registered source, BLE or otherwise, is drained by that one task. Each source gets its own static frame buffer (`s_rx_frames[slot][SYS_INTERFACE_RX_FRAME_CAP]`), so `max_frame_len` must be `<= SYS_INTERFACE_RX_FRAME_CAP` (512). Whether a given source's producer ever gives `sys_interface_get_rx_wake_sem()` is entirely up to that producer - the registry itself carries no wake metadata.

Fails with `ERR_NULL_PTR` if `dequeue_fn` is `NULL`, `ERR_INVALID_VAL_UI32` if `max_frame_len` is out of range, `ERR_INTERFACE_NO_SOURCE_SLOTS` if the registry is full, or `ERR_BASE_NO_MEM` if the receiver task fails to start (first registration only).

### `err_h sys_interface_bind_ble_rx(uint16_t char_uuid, size_t max_frame_len)`
Attaches a BLE characteristic's RX buffer as a frame source, via `sys_interface_register_rx_source()`. To get a near-instant wake for this source, create the characteristic with `rx_notify_sem = sys_interface_get_rx_wake_sem()` - `sys_ble` then gives that handle on every peer write, entirely independent of this call. May be called more than once, for different characteristics — every registered source (BLE or otherwise) shares the same receiver task and the same `SYS_INTERFACE_MAX_RX_SOURCES` budget.

Fails with `ERR_BASE_INVALID_STATE` if the characteristic exists but was created with `rx_buffer_size == 0`, the `sys_ble` lookup error, or any error from `sys_interface_register_rx_source()`. The binding is permanent — there is no unbind.

### `err_h sys_interface_tap_enable(size_t buf_size)` / `void sys_interface_tap_disable(void)` / `err_h sys_interface_tap_poll(uint8_t* buf, size_t max_len, size_t* out_len)`
A pull-based frame observer: `tap_enable()` allocates a `sys_buff_t` ring buffer (`buf_size` bytes); from then on, the RX receiver task pushes a copy of every live frame it decodes into that buffer (right where the old push-callback tap used to fire - class byte included, before dispatch, never for a direct `sys_interface_decode()` call like replay). A consumer calls `tap_poll()` from its own task, in a loop, until `*out_len == 0`, to drain it at its own pace. Unlike the old design, the tap never runs consumer code inline inside the receiver task, so a slow or blocking consumer can't stall RX - the worst case is the tap buffer fills up and new tapped frames are dropped (logged), while live dispatch is unaffected. Only one tap buffer exists; `tap_enable()` while already enabled is a no-op. This is what [[SYS_ACTIONS.MD]] uses to record live traffic without sys_interface knowing anything about actions - see `sys_actions_tap_task`.

### `void sys_interface_suspend_rx(void)` / `void sys_interface_resume_rx(void)` / `bool sys_interface_is_rx_suspended(void)`
Nesting-safe suspend of the RX receiver's draining, same depth-counter shape as `SE_suspend()`/`SE_resume()` in [[SYS_ERRORS.MD]]. This is a **best-effort signal, not a hard barrier**: while suspended, the receiver task does not drain any registered source at all - frames simply accumulate in each source's own buffer (e.g. a BLE characteristic's `rx_buff`) up to that buffer's own capacity, instead of being dropped. If a producer actually gave the wake semaphore while suspended, the receiver waits 1ms (so it doesn't spin) and gives it back before looping, so resume notices the still-pending data within ~1ms instead of waiting for the next full poll tick. Used by `sys_actions_invoke()` so a replayed packet sequence can't interleave with live incoming traffic.

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

Registering a non-BLE source (can happen any time, e.g. once a LoRa driver comes up post-boot). Not giving `sys_interface_get_rx_wake_sem()` anywhere means this source is picked up on the next `SYS_INTERFACE_RX_WAIT_MS` tick rather than instantly - fine for a first cut, and nothing else needs to change later if the LoRa driver's own ISR starts giving that semaphore too:

```c
static err_h lora_rx_dequeue(void* ctx, uint8_t* buf, size_t max_len, size_t* out_len) {
  return lora_pop_frame((lora_dev_t*)ctx, buf, max_len, out_len);  // non-blocking
}

CHECK_AND_LOG(sys_interface_register_rx_source(lora_rx_dequeue, &s_lora_dev, 256, "lora"));
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
| `ERR_INTERFACE_NO_CLASS_SLOTS` | `uint8_t class_header;` | Class registry full. |
| `ERR_INTERFACE_ENC_BUF_TOO_SMALL` | `uint32_t got; uint32_t need;` | An encoder's destination buffer cannot hold even one record. |
| `ERR_INTERFACE_NO_SOURCE_SLOTS` | `uint8_t unused;` | RX source registry full (`SYS_INTERFACE_MAX_RX_SOURCES`). |

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
