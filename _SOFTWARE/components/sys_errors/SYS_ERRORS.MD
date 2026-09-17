# System Error Handling (sys_errors)

IMPORTANT: If you modify the source files in this component (sys_error.h, sys_error.c, sys_error_handler.c, or any of the codes/ mappings), you must update this documentation to reflect the changes.

## Overview
The `sys_errors` component implements the unified error-reporting mechanism used throughout the runIT firmware. Errors are represented as linked-list chains of ring-buffer-allocated nodes (`err_h`), each tagged with an error code (`err_tag_e`), an owning module (`sys_owner_e`), and a small fixed-layout payload describing the failure context. Chains are built bottom-up as a low-level failure propagates back through call sites, then handed off to a background task for logging/telemetry at the point where the chain reaches its origin call.

It also owns the firmware's **outbound diagnostics policy** (`SE_configure()`): the ESP log level, whether log lines are mirrored on serial and/or streamed over BLE, and whether error chains are encoded and transmitted for client-side reconstruction. See [Logging & Telemetry Configuration](#logging--telemetry-configuration) below.

The component is split into four `.c` files along functional lines:
* `sys_error.c`: Ring buffer allocator, name lookups, error pointer validation, and root cause resolution (`SE_get_error_root`).
* `sys_error_config.c`: Outbound diagnostics/telemetry configuration (`SE_configure`, `SE_get_config`).
* `sys_error_log.c`: BLE log forwarding, the `esp_log` `vprintf` hook (`se_log_vprintf`), and serial error chain expansion (`se_log_error_chain` / `log_chain_to_serial`).
* `sys_error_handler.c`: Error dispatch and transport sink registration.

### Directory Map
```
components/sys_errors/
├── CMakeLists.txt
├── SYS_ERRORS.MD           # This documentation
├── sys_error.c             # Ring buffer allocator, name lookups, SE_get_error_root
├── sys_error_config.c      # Diagnostics config implementation (SE_configure, SE_get_config)
├── sys_error_log.c         # BLE log hook, formatting, and chain expansion (se_log_error_chain)
├── sys_error_handler.c     # Error dispatch & sink registration
├── include/
│   ├── sys_error.h         # Core types, SE_* macros, API declarations
│   ├── sys_error_config.h  # Configuration types/defaults and transport binding (SE_bind_ble_tx)
│   └── sys_error_log.h     # Internal log hook configuration interface
└── codes/
    ├── sys_error_codes.h   # Aggregates all module error/owner maps into SYS_ERROR_MAP / SYS_OWNER_MAP
    ├── devices_owners.h    # Owner IDs for device drivers/adapters (PROVIDER_OWNER_MAP)
    └── base/
        └── sys_error_base.h # Base/common error tags shared by all modules (SYS_ERROR_BASE_MAP)
```

Each system submodule (`sys_device`, `sys_io`, `sys_i2c`, `sys_power`, `ble`, `sys_interface`, `sys_buffers`, `sys_actions`, `VM`) defines its own `sys_error_<module>.h` under its own `include/` directory, declaring a `SYS_ERROR_<MODULE>_MAP(X)` and `SYS_<MODULE>_OWNER_MAP(X)`. `sys_error_codes.h` includes each of these headers and folds them into the two global X-macros below, so this component's own INCLUDE_DIRS list every submodule's `include/` path to resolve them.

Two exceptions to "under its own `include/`": the VM's map lives at `components/VM/core/errors/sys_error_vm.h`, and the error *encoder* at `components/codecs/encoders/`. Both are reached by a raw include path in this component's CMakeLists rather than `REQUIRES VM` / `REQUIRES codecs`, because `VM` and `codecs` already require `sys_errors` — a component dependency back would be circular. The build emits a CMake warning about each of these include dirs "belonging to" another component; that is expected and is the tradeoff for breaking the cycle.

---

## Core Data Structures

### sys_err_t / err_h
An error node is a variable-length, ring-buffer-allocated struct with a flexible array member payload, chained to its cause via `next_cause`.
```c
typedef struct err_node {
  err_tag_e tag;
  uint32_t owner;
  struct err_node* next_cause;
  uint8_t payload[] __attribute__((aligned(8)));
} sys_err_t;

typedef sys_err_t* err_h;
```
`err_h == NULL` always means success; this is what `SE_IS_OK` / `SE_IS_ERR` check.

### err_tag_e and payload structs (auto-generated)
`SYS_ERROR_MAP(X)` is expanded once to generate the `err_tag_e` enum (one entry per error tag) and once more to generate one payload struct per tag (`err_payload_<TAG>_t`), so adding a new error tag only requires adding one line to a module's map macro.

### sys_owner_e (auto-generated)
`SYS_OWNER_MAP(X)` similarly generates the `sys_owner_e` enum of module/owner IDs (e.g. `OWNER_DEVICE_INA3221`, `OWNER_SYS_IO_BASE`), used to tag which module raised a given error node.

---

## Ring Buffer Allocator (sys_error.c)

Error nodes are allocated from a fixed-size (`ERR_BUF_SIZE`, 2048 bytes) lock-free ring buffer via atomic compare-exchange bump allocation (`SE_alloc_bytes`) — there is no free; slots are simply overwritten once the head wraps around, so error chains must be logged/consumed promptly. A `_Static_assert` per tag (`X_CHK` in sys_error.c) guarantees no single payload can exceed 1/4 of the ring, bounding how much of the buffer one allocation (or one chain) can occupy.

### Pointer Validation & Safe Traversal (`SE_get_error_root`)
Because older nodes in the ring buffer can be overwritten if the head wraps before a chain is consumed, `sys_errors` protects against corrupted or circular `next_cause` pointers:
* **`SE_is_valid_error_ptr(err)`**: Verifies that an error pointer is 8-byte aligned and strictly contained within `[err_buffer, err_buffer + ERR_BUF_SIZE - sizeof(sys_err_t)]`. Guarantees no Out-of-Bounds (OOB) memory reads occur even if an old node was overwritten with arbitrary payload data.
* **`SE_get_error_root(err)`**: Walks the `next_cause` chain down to the deepest cause node. Bounded by `SE_MAX_CHAIN_DEPTH` (16) and validates every `next_cause` pointer using `SE_is_valid_error_ptr()` and self-reference checks (`next != root`). Returns `NULL` if the chain contains a cycle, exceeds 16 nodes, or points to invalid/corrupted memory.
* Both `enc_sys_errors_encode_chain()` and `se_log_error_chain()` (`log_chain_to_serial`) also use `SE_is_valid_error_ptr()` to break traversal early if an unconsumed node was corrupted by ring wrap.

## Error Handler (sys_error_handler.c)

> [!NOTE]
> The asynchronous FreeRTOS background task (`sys_error_handler_task`) and queue (`s_err_queue`) have been removed from the project. Error handling will be rebuilt using synchronous error dispatch. For now, `SE_init()` and `SE_push_to_handler()` are safe no-ops, preserving API compatibility across the firmware.

`SE_push_to_handler(err)` will invoke the synchronous handler once rebuilt; it remains ISR-safe and a no-op while suspended (`SE_suspend()` / `SE_resume()`, nesting-safe via a depth counter, implemented in `sys_error.c`).

### Outbound Transport Sink

The handler never names a transport. A transport binds itself:

```c
typedef err_h (*se_tx_sink_f)(void* ctx, uint8_t header, const uint8_t* data, size_t len);
void SE_register_tx_sink(se_tx_sink_f send_fn, void* ctx, const char* name);
```

This is the **mirror image of the RX-source registry in [[SYS_INTERFACE.MD]]**. There, a transport hands the router a non-blocking `dequeue_fn(ctx, ...)` and inbound frames find their way in; here, a transport hands the handler a `send_fn(ctx, ...)` and outbound error packets find their way out. Both sides hold nothing but a function pointer and an opaque context, so neither component has to depend on `ble` (or on whatever transport comes next) to do its job.

One slot, assigned outright — unlike `sys_interface`'s source registry there is no fan-out and nothing to fail, so it returns nothing: rebinding replaces, and a NULL `send_fn` unbinds. `ctx` is published before `send_fn`, so the task can never pick up a new sink paired with the previous one's context. While no sink is bound the stream is simply off.

The sink is called from the handler task only, never from an ISR, once per dequeued chain. **Its return value is dropped**: reporting a transport failure through the transport that just failed only loops.

### Transport Binding via `sys_data_connector`
Transport providers are supplied by `sys_data_connector` adapters. Board-specific connector wiring lives in `runit_board_cfg.h` (`runit_data_connector_static_config()`). Sinks are registered generically with `se_register_log_sink()` and `se_register_errors_sink()`, keeping `sys_errors` independent of concrete transports.

### What the skeleton dropped

One thing used to happen in the handler task and no longer does. It is intended to come back in some form; it has no replacement today. (The serial stack trace that used to be listed here is back — see step 2 above.)

| Removed | What it did | What it leaves behind |
| :--- | :--- | :--- |
| **Device-error dispatch** | `dispatch_device_owned_error()` scanned each chain for the first `ERR_DEV_DEP_FAILED` node owned in the device-provider range (`0xD0xx`), pulled `dev_id` out of its payload, and called a hook registered via `SE_register_device_error_hook()` — which `sys_device` filled in from a load-time constructor with `sys_device_report_error()`. A chain tagged `ERR_DEV_FAULT_RESPONSE_FAILED` was skipped so a failed fault response couldn't be fed back into the policy that produced it. | `SE_register_device_error_hook()` / `se_device_error_hook_t` are gone from `sys_error.h`, and `sys_device.c`'s constructor with them. **`sys_device_report_error()` now runs only when something calls it directly**, so per-device error handling and the `sys_actions` fault policy no longer fire off ordinary error traffic — see [[SYS_DEVICE.MD]]. Restoring it means restoring the `ERR_DEV_FAULT_RESPONSE_FAILED` guard too. |

---

## Logging & Telemetry Configuration

Responsibilities are strictly partitioned between `sys_error_config` and `sys_error_log`:
* **`sys_error_config.c` / `sys_error_config.h`**: Manages outbound error telemetry configuration (`sys_error_cfg_t.errors`, packet size clamping, TX header byte, and BLE transport sink adapter `SE_bind_ble_tx()`).
* **`sys_error_log.c` / `sys_error_log.h`**: Manages all logging configuration, sink registry, text log forwarding (`se_log_vprintf`), serial error chain expansion (`se_log_error_chain`), and raw packet dispatch (`se_log_send_raw`, `se_log_send_error_raw`).

### `sys_error_cfg_t`
The error telemetry configuration struct carries error packet routing switches and constraints:

```c
typedef struct {
  struct {
    uint8_t  tx_header;              // TX stream header handed to the sink (see SE_register_tx_sink())
    uint16_t packet_max;             // clamped to SE_ERR_PACKET_MAX (244)
  } errors;
} sys_error_cfg_t;
```

`SYS_ERROR_CFG_DEFAULT()` provides the pre-`SE_configure()` default (`tx_header = 0`, `packet_max = SE_ERR_PACKET_MAX`).

### `err_h SE_configure(const sys_error_cfg_t* cfg)`
Applies error telemetry configuration. Clamps `errors.packet_max` to `SE_ERR_PACKET_MAX`. Returns `ERR_NULL_PTR` if `cfg == NULL`.

### `err_h SE_get_config(sys_error_cfg_t* out_cfg)`
Reads back the error telemetry configuration in force (post-clamping).

### Logging Controls (`sys_error_log.c`)

Log filtering and sinks are configured independently of `sys_error_cfg_t`:

* **`SE_set_logging(esp_log_level_t level, bool mirror_serial, bool trace_errors)`**: Single unified setter designed for direct control from `sys_interface` packets, scripts, or local tasks. Validates `level`, sets `esp_log_level_set("*", level)`, and updates serial mirroring and error chain trace toggles in a single call. Returns `NULL` on success or `ERR_INVALID_VAL_UI32` if `level > ESP_LOG_VERBOSE`.
* **`se_set_log_connector(conn)`**: Binds a `sys_data_connector_t*` instance (e.g. `sys_data_connector_get(CONN_ID_LOGS)`) directly as the destination for ESP_LOG text logs.
* **`se_set_errors_connector(conn)`**: Binds a `sys_data_connector_t*` instance (e.g. `sys_data_connector_get(CONN_ID_ERRORS)`) directly as the destination for encoded binary error telemetry packets.
* **`se_register_log_sink(sink_fn, ctx, name)`**: Legacy/test transport sink registration for formatted text log forwarding (ESP_LOG only via `se_log_vprintf`).
* **`se_register_errors_sink(sink_fn, ctx, name)`**: Legacy/test transport sink registration for encoded binary error packets.

> **Note on Transport Bindings**:
> Concrete transports are attached through `sys_data_connector`; runIT's BLE mappings are configured by `runit_data_connector_static_config()` in `runit_board_cfg.h`.

### Error Packet Dispatch (`sys_error_log.c`)

`sys_error_log.c` provides error packet dispatch over configured diagnostics sinks:
* **`SE_send(err_h chain)`**: Unified error dispatcher that invokes both `se_log_error_chain(chain)` (formatted text logging via ESP_LOG) and `SE_send_error_raw(chain)` (encoded binary telemetry over the errors connector / sink) according to current configuration.
* **`SE_send_error_raw(err_h chain)`**: Encodes an error chain into binary format using `enc_sys_errors_encode_chain()` and transmits the packet to the registered errors connector and/or sink.

### The log hook (`sys_error_log.c`)
`se_log_vprintf()` replaces the default `vprintf` sink and does two things:
1. Renders the line into a `SE_LOG_LINE_MAX` (256 B) buffer and forwards it to the bound log connector (and/or registered log sink).
2. Forwards the **original** `va_list` to serial `vprintf` when `mirror_on_serial` is enabled. Serial therefore keeps full fidelity — only remote transport packets are truncated at `SE_LOG_LINE_MAX`.

A boolean re-entrancy flag guards recursion: connector/sink send paths may log on their own error paths, and that log comes straight back into the hook. A nested call finds the flag set and takes the serial-only path instead of recursing. Under contention another task's line is dropped from the external stream (never from serial).

### Usage

```c
SE_init();

// 1. Configure error telemetry limits & header
CHECK_AND_LOG(SE_configure(&(sys_error_cfg_t){
    .errors = {
        .tx_header = PACKET_HEADER_ERRORS,
        .packet_max = SE_ERR_PACKET_MAX,
    },
}));

// 2. Configure logging rules
SE_set_logging(ESP_LOG_INFO, true, true);

// 3. Connect to transport layer (sys_data_connector)
runit_data_connector_static_config();
se_set_log_connector(sys_data_connector_get(CONN_ID_LOGS));
se_set_errors_connector(sys_data_connector_get(CONN_ID_ERRORS));
```

---

## Core Macros (implicitly use `OWNER`)

Every `.c` file using these macros must `#define OWNER OWNER_<MODULE_NAME>` near the top, per the [[runit]] skill's module owner tagging convention.

* **`SE_IS_OK(call)`** / **`SE_IS_ERR(call)`**: NULL-checks on an `err_h`.
* **`SE_ERR_NEW(tag_name, ...)`**: Allocates and fills a new leaf error node for `tag_name` with designated-initializer payload fields; `next_cause` is left NULL.
* **`SE_WRAP_ERR(rc_err, tag_name, ...)`**: Creates a new node for `tag_name` and chains `rc_err` as its cause.
* **`SE_WRAP_DEV_ERR(rc_err, dep_dev_id)`**: Shorthand for wrapping a dependency failure as `ERR_DEV_DEP_FAILED` with a `dev_id` payload.
* **`SE_PASS_ON_ERR(call, tag_name, ...)`**: Evaluates `call`; if it returns a non-NULL `err_h`, wraps it under `tag_name` and returns the wrapped chain immediately.
* **`SE_RET_IF_ERR(call)`**: `SE_PASS_ON_ERR` specialized to wrap under the generic `ERR_DEP_FAILED` tag — the default choice for propagating a nested call's error without adding origin-specific context.
* **`SE_SET_ERR(out_err, tag_name, ...)`**: Assigns a freshly built error node to an out-parameter instead of returning it.
* **`SE_EMIT_ERR(tag_name, ...)`**: Builds a node and immediately pushes it to the handler without returning it (fire-and-forget diagnostic).
* **`SE_RET_ERR(tag_name, ...)`**: Builds a node and returns it directly (used at the true point of failure).
* **`SE_ORIGIN_CALL(call)`**: Wraps a top-level entry point call; if it returns a non-NULL chain, pushes the whole chain to the handler. This is where a chain actually reaches the logging task, per the skill's `SE_ORIGIN_CALL` convention for entry points like `runit_start()`.

---

## Utility / Compatibility Macros

* **`SE_CONVERT_ESP(esp_call)`**: Evaluates an `esp_err_t` expression; returns `NULL` on `ESP_OK`, otherwise a new `ERR_ESP_ERR` node carrying the raw `esp_code`.
* **`SE_RET_IF_ESP_ERR(esp_call)`**: Same check as above, but returns the `ERR_ESP_ERR` node directly instead of yielding it as a value.
* **`SE_CHECK_NOT_NULL(ptr)`**: Returns `ERR_NULL_PTR` if `ptr` is NULL.
* **`SE_CHECK_IF_ALLOCATED(ptr)`**: Returns `ERR_BASE_NO_MEM` if `ptr` is NULL (post-allocation check).
* **`SE_CHECK_HANDLE(handle)`**: Returns `ERR_NO_HANDLE` if `handle` is NULL.
* **`SE_CHECK_IN_RANGE(val, min, max)`**: `_Generic`-dispatched bounds check (float/double, signed integer, or default-unsigned variants) that returns an `ERR_INVALID_VAL_*` node with `val`/`min`/`max` payload on violation. Bounds must share `val`'s signedness — the dispatch keys off `val`'s type only, so a negative bound paired with an unsigned `val` will route to the unsigned variant and silently wrap.

---

## Base Error Tags (SYS_ERROR_BASE_MAP)
Common tags available to every module regardless of submodule-specific maps: `ERR_NO_HANDLE`, `ERR_NULL_PTR`, `ERR_INVALID_VAL_UI32`, `ERR_INVALID_VAL_I32`, `ERR_INVALID_VAL_F`, `ERR_DEV_DEP_FAILED`, `ERR_DEP_FAILED`, `ERR_ESP_ERR`, `ERR_BASE_NO_MEM`, `ERR_BASE_NOT_SUPPORTED`, `ERR_BASE_NOT_FOUND`, `ERR_BASE_INVALID_STATE`.

## Own Owners (SYS_ERRORS_OWNER_MAP)
| Owner | ID |
| :--- | :--- |
| `OWNER_SYS_ERRORS_BASE` | `0xA800` |
| `OWNER_SYS_ERRORS_CONFIG` | `0xA801` |

---

## API Summary

```c
void SE_init(void);
err_h SE_alloc_bytes(size_t payload_size, err_tag_e tag, uint32_t owner);
void SE_push_to_handler(err_h err);
uint32_t SE_get_dropped_count(void);
void SE_clear_dropped_count(void);
void SE_register_tx_sink(se_tx_sink_f send_fn, void* ctx, const char* name);

// sys_error_config.h (header-only transport binding)
static inline void SE_bind_ble_tx(uint16_t char_uuid);

void SE_suspend(void);
void SE_resume(void);
bool SE_is_suspended(void);

const char* SE_get_owner_name(uint32_t owner);
const char* SE_get_tag_name(err_tag_e tag);
size_t SE_get_payload_size(err_tag_e tag);   // sizeof(err_payload_<tag>_t); 0 if unknown
bool SE_describe_payload(err_tag_e tag, const void* payload, char* out, size_t out_size);  // true if tag has a registered description

bool SE_is_valid_error_ptr(err_h err);
err_h SE_get_error_root(err_h error);

err_h SE_configure(const sys_error_cfg_t* cfg);
err_h SE_get_config(sys_error_cfg_t* out_cfg);
```

`SE_get_payload_size()` exists because a node does not store its own payload length — it is a property of the tag. The encoder uses it both to know how many bytes after `sys_err_t` belong to the node and to put that length on the wire.

Two `_Static_assert` blocks in `sys_error.c` keep `err_tag_e` and every owner ID inside the `uint16` fields of the error wire format.

---

## Adding New Error Codes & Owners

1. **Declare the map** in the owning module's own `sys_error_<module>.h` (or `codes/devices_owners.h` for device drivers, `codes/base/sys_error_base.h` for cross-cutting base tags):
   ```c
   #define SYS_ERROR_MY_MODULE_MAP(X) \
     X(ERR_MY_MODULE_THING, struct { uint32_t some_field; })
   #define SYS_MY_MODULE_OWNER_MAP(X) \
     X(OWNER_MY_MODULE_BASE, 0xE000, "OWNER_MY_MODULE_BASE")
   ```
2. **Register the map** in `components/sys_errors/codes/sys_error_codes.h` by including the header and adding both macros to `SYS_ERROR_MAP` / `SYS_OWNER_MAP`. This automatically extends `err_tag_e`, `sys_owner_e`, the generated payload structs, and the `SE_get_owner_name` / `SE_get_tag_name` lookup tables.
3. **(Optional) Add a description**, in the same `sys_error_<module>.h`: add `ERR_MY_MODULE_THING` to a `SYS_ERROR_MY_MODULE_LOGGER_MAP(X)` (`X(ERR_MY_MODULE_THING)`) and define `LOG_BODY_ERR_MY_MODULE_THING(p, out, out_size)`, then add `SYS_ERROR_MY_MODULE_LOGGER_MAP(X)` as one more line in `sys_error_codes.h`'s `SYS_ERROR_LOGGER_MAP`. See [Per-Tag Payload Descriptions](#per-tag-payload-descriptions-se_describe_payload) above. Skippable — an undescribed tag just falls back to a hex payload dump.
