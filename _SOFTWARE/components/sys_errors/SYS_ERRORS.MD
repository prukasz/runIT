# System Error Handling (sys_errors)

IMPORTANT: If you modify the source files in this component (sys_error.h, sys_error.c, or any of the codes/ mappings), you must update this documentation to reflect the changes.

## Overview
The `sys_errors` component implements the unified error-reporting mechanism used throughout the runIT firmware. Errors are represented as linked-list chains of ring-buffer-allocated nodes (`err_h`), each tagged with an error code (`err_tag_e`), an owning module (`sys_owner_e`), and a small fixed-layout payload describing the failure context. Chains are built bottom-up as a low-level failure propagates back through call sites, then handed off to a background task for logging/telemetry at the point where the chain reaches its origin call.

### Directory Map
```
components/sys_errors/
├── CMakeLists.txt
├── SYS_ERRORS.MD           # This documentation
├── sys_error.c             # Ring buffer allocator, error queue task, name lookups
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

## Ring Buffer Allocator & Error Task

Error nodes are allocated from a fixed-size (`ERR_BUF_SIZE`, 1024 bytes) lock-free ring buffer via atomic compare-exchange bump allocation (`SE_alloc_bytes`) — there is no free; slots are simply overwritten once the head wraps around, so error chains must be logged/consumed promptly. A `_Static_assert` per tag (`X_CHK` in sys_error.c) guarantees no single payload can exceed 1/4 of the ring, bounding how much of the buffer one allocation (or one chain) can occupy.

`SE_init()` lazily creates a static FreeRTOS queue (`ERR_QUEUE_LEN` = 32) and a background task (`sys_error_handler_task`) that drains completed chains and logs each node in the chain (owner name, owner ID, tag name, tag ID) via `ESP_LOGE`.

`SE_push_to_handler(err)` enqueues a completed chain; it is ISR-safe (uses `xQueueSendFromISR` when called from ISR context) and is a no-op while suspended (`SE_suspend()` / `SE_resume()`, nesting-safe via a depth counter) or before `SE_init()` has run.

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
```

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
