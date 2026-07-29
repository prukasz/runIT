# System Error Handling (sys_errors)

IMPORTANT: If you modify the source files in this component (sys_error.h, sys_error.c, sys_error_handler.c, or any of the codes/ mappings), you must update this documentation to reflect the changes.

## Overview
The `sys_errors` component implements the unified error-reporting mechanism used throughout the runIT firmware. Errors are represented as linked-list chains of ring-buffer-allocated nodes (`err_h`), each tagged with an error code (`err_tag_e`), an owning module (`sys_owner_e`), and a small fixed-layout payload describing the failure context. Chains are built bottom-up as a low-level failure propagates back through call sites, then handed off to a background task for logging/telemetry at the point where the chain reaches its origin call.

It also owns the firmware's **outbound diagnostics policy** (`SE_configure()`): the ESP log level, whether log lines are mirrored on serial and/or streamed over BLE, and whether error chains are encoded and transmitted for client-side reconstruction. See [Logging & Telemetry Configuration](#logging--telemetry-configuration) below.

The component is split into two `.c` files along a stable/expanding-surface line: `sys_error.c` holds the ring buffer allocator, name lookups, and diagnostics config/log hook — all narrow, unlikely to grow — while `sys_error_handler.c` owns the queue, the consumer task, and how a completed chain gets dispatched (currently: serial trace + BLE encode/send). Handler dispatch is the part expected to grow (more sinks, filtering, routing), so it's isolated in its own file rather than mixed into the allocator.

### Directory Map
```
components/sys_errors/
├── CMakeLists.txt
├── SYS_ERRORS.MD           # This documentation
├── sys_error.c             # Ring buffer allocator, name lookups, diagnostics config, log hook
├── sys_error_handler.c      # Error queue, consumer task, SE_init(), SE_push_to_handler()
├── include/
│   └── sys_error.h         # Core types, SE_* macros, API declarations
└── codes/
    ├── sys_error_codes.h   # Aggregates all module error/owner maps into SYS_ERROR_MAP / SYS_OWNER_MAP
    ├── devices_owners.h    # Owner IDs for device drivers/adapters (PROVIDER_OWNER_MAP)
    └── base/
        └── sys_error_base.h # Base/common error tags shared by all modules (SYS_ERROR_BASE_MAP)
```

Each system submodule (`sys_device`, `sys_io`, `sys_i2c`, `sys_power`, `ble`, `sys_interface`, `sys_buffers`) defines its own `sys_error_<module>.h` under its own `include/` directory, declaring a `SYS_ERROR_<MODULE>_MAP(X)` and `SYS_<MODULE>_OWNER_MAP(X)`. `sys_error_codes.h` includes each of these headers and folds them into the two global X-macros below, so this component's own INCLUDE_DIRS list every submodule's `include/` path to resolve them.

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

Error nodes are allocated from a fixed-size (`ERR_BUF_SIZE`, 1024 bytes) lock-free ring buffer via atomic compare-exchange bump allocation (`SE_alloc_bytes`) — there is no free; slots are simply overwritten once the head wraps around, so error chains must be logged/consumed promptly. A `_Static_assert` per tag (`X_CHK` in sys_error.c) guarantees no single payload can exceed 1/4 of the ring, bounding how much of the buffer one allocation (or one chain) can occupy.

## Error Queue & Handler Task (sys_error_handler.c)

The error queue (`s_err_queue`, `ERR_QUEUE_LEN` = 32) is a static FreeRTOS queue defined via the `utils` component's `R_QUEUE_DEFINE` — it's live before `app_main()` runs (constructor-initialized), so nothing needs to guard its creation, including `SE_push_to_handler()` calls that happen to land before `SE_init()`. `SE_init()` starts the background consumer task (`sys_error_handler_task`), statically allocated via `R_TASK_DEFINE` / `R_TASK_START` (see [[runit]] skill), guarded so a repeat call doesn't start it twice.

The task reads a fresh copy of the config via `SE_get_config()` each time it wakes (rather than reaching into `sys_error.c`'s internal `s_cfg`, keeping the two files decoupled to the public API) and, per that configuration, (a) encodes the chain into a wire packet, (b) logs each node (owner name, owner ID, tag name, tag ID) via `ESP_LOGE`, and (c) pushes the encoded packet to a BLE TX slot — **in that order**, because both (b) and (c) can themselves allocate from the same ring the chain lives in.

`SE_push_to_handler(err)` enqueues a completed chain; it is ISR-safe (uses `xQueueSendFromISR` when called from ISR context) and is a no-op while suspended (`SE_suspend()` / `SE_resume()`, nesting-safe via a depth counter, implemented in sys_error.c). A push that happens before `SE_init()` has started the task simply waits in the queue rather than being dropped.

This is the file to extend for new dispatch behavior (additional sinks, per-owner filtering, rate limiting, etc.) — see the note in the Overview above.

---

## Logging & Telemetry Configuration

### `sys_error_cfg_t`
One struct decides where both outbound streams go. It carries **routing switches only** — no per-sink level fields. Level selection belongs to `esp_log` (`global_level`, plus any per-tag `esp_log_level_set()` calls the application makes), so a line that reaches the hook is a line every enabled sink wants. That keeps the struct flat and small enough to be applied remotely as a single packet payload, in the style of the decoder structs in [[CODECS.MD]].

```c
typedef struct {
  esp_log_level_t global_level;      // applied as esp_log_level_set("*", ...) - the only level filter
  struct {
    bool     mirror_on_serial;       // keep writing lines to the original sink (UART/stdout)
    bool     ble_enable;             // also push each rendered line to a characteristic
    uint16_t char_uuid;              // required when ble_enable
    uint8_t  tx_header;              // TX slot header identifying the log stream
  } logs;
  struct {
    bool     serial_trace;           // ESP_LOGE the decoded owner/tag stack trace
    bool     ble_enable;             // encode chains and push them to a characteristic
    uint16_t char_uuid;              // required when ble_enable
    uint8_t  tx_header;              // TX slot header identifying the error stream
    uint16_t packet_max;             // clamped to SE_ERR_PACKET_MAX (244)
  } errors;
} sys_error_cfg_t;
```

`SYS_ERROR_CFG_DEFAULT()` is the pre-`SE_configure()` state: serial only, nothing on BLE — a board that never configures behaves exactly as it did before telemetry existed.

### `err_h SE_configure(const sys_error_cfg_t* cfg)`
Applies a configuration. Idempotent, callable any time after `SE_init()`, but must run **after** the BLE characteristics and TX slots it names exist. Returns `ERR_NULL_PTR`, or `ERR_INVALID_VAL_UI32` when a stream is enabled with `char_uuid == 0`. Re-points the `esp_log` `vprintf` hook to `se_log_vprintf` on every call (a no-op past the first, since it's always the same function); the previous hook is never captured, so serial output always ends up as plain `vprintf`. Clearing both `logs.mirror_on_serial` and `logs.ble_enable` silences logs entirely — the hook still runs and drops every line.

### `err_h SE_get_config(sys_error_cfg_t* out_cfg)`
Reads back the configuration in force, after clamping.

### The log hook
`se_log_vprintf()` replaces the default `vprintf` sink and does exactly two things: render the line into a `SE_LOG_LINE_MAX` (256 B) stack buffer and `sys_ble_char_send()` it when `logs.ble_enable`, then forward the **original** `va_list` to the previous sink when `logs.mirror_on_serial`. Serial therefore keeps full fidelity — only the BLE copy is truncated at `SE_LOG_LINE_MAX`.

> **Depends on `CONFIG_LOG_VERSION_1`** (what this project builds with): log v1 delivers one whole line per `vprintf` call, so one call maps to one BLE notification with no line reassembly. Log **v2** splits a line across three calls (prefix / user format / newline+reset) and would emit three fragments per line — switching to v2 means reintroducing an accumulator that flushes on `'\n'`.

A `__atomic_exchange_n` flag guards re-entrancy: `sys_ble_char_send()` logs on its own error paths, and that log comes straight back into the hook. A nested call finds the flag set and takes the serial-only path instead of recursing. Under contention another task's line is dropped from the BLE stream (never from serial).

### Error telemetry
When `errors.ble_enable` is set, the handler task encodes each chain with `enc_sys_errors_encode_chain()` (see [[CODECS.MD]]) and pushes the packet to `errors.char_uuid` / `errors.tx_header`. `err_h` values returned by `sys_ble_char_send()` on either stream are deliberately dropped — feeding a TX failure back into the error queue would log from inside the log path.

### Usage

```c
SE_init();
CHECK_AND_LOG(sys_ble_static_config());   // creates the characteristics/TX slots first
CHECK_AND_LOG(SE_configure(&(sys_error_cfg_t){
    .global_level = ESP_LOG_INFO,
    .logs   = {.mirror_on_serial = true, .ble_enable = true,
               .char_uuid = SYS_BLE_CHR_RUNIT_LOGS, .tx_header = PACKET_HEADER_LOGS},
    .errors = {.serial_trace = true, .ble_enable = true,
               .char_uuid = SYS_BLE_CHR_RUNIT_LOGS, .tx_header = PACKET_HEADER_ERRORS,
               .packet_max = SE_ERR_PACKET_MAX},
}));

// Narrowing or widening a single module stays an esp_log concern:
esp_log_level_set("sys_i2c", ESP_LOG_DEBUG);
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

void SE_suspend(void);
void SE_resume(void);
bool SE_is_suspended(void);

const char* SE_get_owner_name(uint32_t owner);
const char* SE_get_tag_name(err_tag_e tag);
size_t SE_get_payload_size(err_tag_e tag);   // sizeof(err_payload_<tag>_t); 0 if unknown

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
