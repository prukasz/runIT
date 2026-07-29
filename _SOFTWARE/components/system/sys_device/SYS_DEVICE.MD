# System Device Manager (sys_device)

IMPORTANT: If you modify the source files in this component (sys_device.h or sys_device.c), you must update this documentation to reflect the changes.

## Overview
The `sys_device` component is the central registry and lifecycle manager for every hardware device instance in runIT. Device drivers/adapters describe themselves declaratively as a `sys_device_class_t` (name, roles, feature contracts, lifecycle ops) and hand a typed config struct to `sys_device_install_cfg()` (via the `SYS_DEVICE_CREATE()` wrapper); `sys_device` owns the registry slot, the heap copy of the config, and dispatch of every subsequent lifecycle/feature call by `device_id`.

### Directory Map
```
components/system/sys_device/
├── CMakeLists.txt
├── SYS_DEVICE.MD         # This documentation
├── sys_device.c          # Registry, install/uninstall, dispatch, suspend/resume/freeze/sync
└── include/
    ├── sys_device.h      # Contracts, class/instance structs, dispatch & adapter-construction macros
    └── sys_error_dev.h   # sys_device owner map and ERR_DEV_* error tag map
```

---

## Architecture & Concept

### Class vs. Instance
* **`sys_device_class_t`** — everything about a device *type* that is a compile-time constant: `name`, `roles` (bitmask of `sys_device_role_e`), the four feature `contracts[]` vtables, and the `sys_device_ops_t` lifecycle function table. Declared once per device type as a `static const`.
* **`sys_device_t`** — one *instance* in the registry: `device_id`, a pointer back to its `cls`, a manager-owned heap copy of the typed config (`cfg`/`cfg_size`), `state`, `device_handle`. Contracts are not copied onto the instance; lookups go through `dev->cls->contracts[]`.

Contracts are declarative: `sys_device` sets `cls` on the instance before running `cls->ops.install` — an adapter never calls something like `sys_io_register_driver()` / `sys_power_register_*()` itself.

`DEV_OP(d, f)` / `DEV_NAME(d)` in sys_device.c are thin shorthands for `d->cls->ops.f` / `d->cls->name`.

### Registry
A fixed array `s_device_registry[MAX_DEVICE_ID + 1]` (128 slots) indexed by `device_id`. Mutations (install/uninstall) are init/config-context only; `sys_device_get_by_id()` is a lock-free read, since it sits on the hot dispatch path and must remain reachable from ISR-adjacent code where a mutex cannot be taken.

### Lifecycle State
```c
typedef enum sys_device_state_e {
  SYS_DEV_STATE_NONE = 0,
  SYS_DEV_STATE_INSTALLING,
  SYS_DEV_STATE_INSTALLED,
  SYS_DEV_STATE_SUSPENDED,
} sys_device_state_e;
```
`INSTALLING` is registry-findable but not ready: `cls` is set and the instance is in the registry before `ops.install` runs, so a dependency device may look this device up mid-install and read its contracts via `dev->cls->contracts[]`, while `SYS_DEV_DISPATCH` / `SYS_DEV_IS_READY` still refuse it until install succeeds. Ordering matters — `SYS_DEV_IS_INSTALLED(d)` tests `state >= SYS_DEV_STATE_INSTALLED`.

### Contracts
```c
typedef enum { SYS_DEVICE_CONTRACT_IO = 0, SYS_DEVICE_CONTRACT_POWER_VREG = 1, SYS_DEVICE_CONTRACT_POWER_MONITOR = 2, SYS_DEVICE_CONTRACT_POWER_USB_PD = 3, SYS_DEVICE_CONTRACT_MAX = 4 } sys_device_contract_type_e;
```
A device's capabilities are determined by its contracts (the four vtable slots above, defined by `sys_io` / `sys_power`). A device may implement more than one contract (e.g. a combined VREG + monitor chip).

### Lifecycle Ops
```c
typedef struct sys_device_ops_t {
  err_h (*install)(const void* cfg, void** out_device_handle);
  err_h (*uninstall)(void* device_handle);
  err_h (*reset)(void* device_handle);
  err_h (*suspend)(void* device_handle);
  err_h (*resume)(void* device_handle);
  err_h (*freeze)(void* device_handle);
  err_h (*sync)(void* device_handle);
  err_h (*error_handler)(void* device_handle, err_h error);
} sys_device_ops_t;
```
Any op left NULL causes the corresponding manager call to return `ERR_BASE_NOT_SUPPORTED` for that device.

---

## Device State Query & Dispatch Macros

* **`SYS_DEV_IS_READY(d)`** / **`SYS_DEV_IS_INSTALLED(d)`** / **`SYS_DEV_IS_SUSPENDED(d)`**: State predicates on a `sys_device_t*`.
* **`SYS_DEV_GET_CONTRACT(dev, type)`**: Fetches `dev->cls->contracts[type]`, or NULL if `dev` is NULL.
* **`IF_SYS_DEV_AND_FEATURE(device_id, contract_type, contract_struct_type, func_member, dev_ptr, vtable_ptr)`**: Single-iteration for-loop pair that looks up the device and its contract vtable, binding `dev_ptr`/`vtable_ptr`, and only enters the body if the device is ready and `func_member` is non-NULL. Used by `sys_io`/`sys_power` to guard a feature call without duplicating the ready/contract/null checks inline.
* **`SYS_DEV_DISPATCH(dev_id, contract_enum, contract_type, func_name, ...)`**: The standard feature-call dispatcher — looks the device up (`ERR_DEV_NOT_FOUND`), checks installed (`ERR_DEV_NOT_INSTALLED`) and not suspended (`ERR_DEV_SUSPENDED`), resolves the contract vtable and function (`ERR_DEV_FEATURE_UNAVAILABLE` if either is missing), then calls it and propagates its result via `SE_RET_IF_ERR`. Must be used inside a function whose body is exactly this macro (it unconditionally `return`s).

---

## Error & Failure Handling Macros

* **`SYS_DEV_CHECK_HANDLE(handle, dev_id)`**: Returns `ERR_DEV_NO_HANDLE` if `handle` is NULL.
* **`SYS_DEV_GET_ADAPTER_CONTEXT(ctx_type, hw_type, ctx_var, hw_var, input_handle)`**: Casts `input_handle` to the adapter's context type, checks it's non-NULL, then extracts and checks its `hw_handle`. The standard first line of every adapter feature function.
* **`RET_IF_DEV_ERR(err_ptr, ctx)`** / **`RET_IF_DEV_INSTALL_FAIL(err_ptr, device_id)`**: Wrap a nested failure as `ERR_DEV_DEP_FAILED` carrying `dev_id`, then return it.
* **`SYS_DEV_CHECK_DRIVER_CALL(driver_call, ctx)`**: Runs a raw driver call, converts an `esp_err_t` failure via `SE_CONVERT_ESP`, and wraps/returns it as a dependency failure tagged with `ctx`'s device_id — the default way to call into a `driver_xxx.c` function from an adapter.
* **`SYS_DEV_INSTALL_FAIL(err, device_id, out_handle, uninstall_fn, ctx)`**: Called once from the `fail:` label of an `install` implementation after `err` is set. NULLs `*out_handle`, and if `ctx` was allocated, suspends error reporting (teardown noise would bury the real cause) and rolls back via `uninstall_fn(ctx)`, then reports `err` wrapped with `device_id`. `uninstall_fn` must tolerate any subset of `ctx`'s fields being unset, and must never early-return on a failing teardown step — it has to free everything regardless.

---

## Adapter Construction & Installation Step Helpers

Adapter contexts are expected to embed a `sys_device_adapter_base_t base` as their first member:
```c
typedef struct {
  void* hw_handle;
  uint8_t device_id;
  bool is_frozen;
  uint16_t steps_done; /* adapter-defined bitmask of completed install steps */
} sys_device_adapter_base_t;
```

* **`SYS_DEV_CTX_NEW(ctx_type, ctx_var, cfg_ptr)`**: Allocates and zero-inits a `ctx_type`, copies `*cfg_ptr` into `ctx_var->cfg`, and sets `ctx_var->base.device_id` from `cfg_ptr->device_id`. Requires `ctx_type`'s first member to be `sys_device_adapter_base_t base`, a `cfg` member matching the config type, and the config's first member to be `uint8_t device_id`. Declares `ctx_var`, so it can't sit inside an unbraced `if`, and must appear before any `goto fail` that would jump over it.
* **`SYS_DEV_STEP_DONE(ctx, bit)`** / **`IF_SYS_DEV_STEP_DONE(ctx, bit)`**: Set/test a bit in `steps_done`, so teardown can roll back exactly what was built instead of inferring it from field sentinels.
* **`SYS_DEV_INSTALL_STEP(expr, what)`**: Runs one install step; on error, logs `what` and `goto fail`. Requires `err_h err`, a `fail:` label, and `TAG` in scope.
* **`SYS_DEV_TEARDOWN_STEP(err_acc, expr)`**: Runs one teardown step, keeping only the *first* error seen in `err_acc`. Never early-returns — every subsequent teardown step still runs even if an earlier one failed.
* **`SYS_DEVICE_CREATE(cls_ptr, cfg_ptr)`**: Shorthand for `sys_device_install_cfg(cls_ptr, cfg_ptr->device_id, cfg_ptr, sizeof(*cfg_ptr))`. Requires `cfg_ptr`'s first member to be `uint8_t device_id`. This is the standard way a `d_xxx_create()` function installs its device.

---

## Manager API Functions

* **`sys_device_install_cfg(cls, device_id, cfg, cfg_size)`**: Heap-copies `cfg`, sets `cls` on the instance, marks it `INSTALLING`, then runs `cls->ops.install`. On failure the registry slot and heap copy are freed and the error is returned; on success state becomes `INSTALLED`. Prefer `SYS_DEVICE_CREATE()`.
* **`sys_device_uninstall(device_id)`**: Removes the registry entry, runs the device's `uninstall` op, then frees the config copy and the instance. Config is freed only *after* the adapter has torn down, since it may still read its own copy of the config until then.
* **`sys_device_reset(device_id)`** / **`sys_device_suspend(device_id)`** / **`sys_device_resume(device_id)`** / **`sys_device_freeze(device_id)`** / **`sys_device_sync(device_id)`**: Single-device lifecycle calls; each checks found/installed/not-suspended (where relevant) before invoking the corresponding op, and returns `ERR_BASE_NOT_SUPPORTED` if the device doesn't implement it. `suspend`/`resume` no-op (return `NULL`) if the device is already in the target state.
* **`sys_device_suspend_all()`** / **`sys_device_resume_all()`** / **`sys_device_freeze_all()`** / **`sys_device_sync_all()`** / **`sys_device_reset_all()`**: Sweep the whole registry, applying the op to every eligible device (ready, or suspended for `resume_all`), stopping and propagating the first failure via `SE_RET_IF_ERR` rather than continuing past it.
* **`sys_device_uninstall_all()`**: Sweeps every populated registry slot and calls `sys_device_uninstall()` on each — same semantics as the single-device call (tolerates any device state, doesn't check ready/suspended).
* **Freeze vs. suspend are orthogonal.** `freeze`/`freeze_all` never touch `state` (an adapter tracks its own `is_frozen` flag via `SYS_DEV_CTX_FREEZE`/`SYS_DEV_CTX_UNFREEZE`); only `suspend`/`suspend_all` set `state = SYS_DEV_STATE_SUSPENDED`. Consequently `resume`/`resume_all` (which only act on devices `SYS_DEV_IS_SUSPENDED`) do **not** clear a freeze that was applied without a suspend — several adapters (see e.g. `device_gpio_esp`'s `device_sync()`) clear `is_frozen` from their `sync` op instead, so lifting a freeze system-wide is `sys_device_sync_all()`, not `sys_device_resume_all()`. [[SYS_STATES.MD]] calls both together when entering its "resume/normal operation" state for exactly this reason.
* **`sys_device_get_by_id(device_id)`**: Lock-free registry lookup; returns `NULL` if out of range or unset.

---

## Error Ownership

Every function in `sys_device.c` sets `#undef OWNER` / `#define OWNER OWNER_SYS_DEVICE_<FUNCTION>` immediately before its body, per the [[runit]] skill's module owner tagging convention — e.g. `sys_device_reset()` tags errors as `OWNER_SYS_DEVICE_RESET`. See `sys_error_dev.h` for the full `SYS_DEVICE_OWNER_MAP` and the `SYS_ERROR_DEV_MAP` tags (`ERR_DEV_NO_HANDLE`, `ERR_DEV_NOT_FOUND`, `ERR_DEV_ALREADY_EXIST`, `ERR_DEV_FEATURE_UNAVAILABLE`, `ERR_DEV_SUSPENDED`, `ERR_DEV_NOT_INSTALLED`, `ERR_DEV_INSTALL_FAILED`).
