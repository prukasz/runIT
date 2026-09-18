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
* **`sys_device_t`** — one *instance* in the registry: `device_id`, a pointer back to its `cls`, a manager-owned heap copy of the typed config (`cfg`/`cfg_size`), `state`, `device_handle`, and its per-instance error handling configuration (`actions[5]`, `importance` — see [Per-Instance Error Handling](#per-instance-error-handling)). Contracts are not copied onto the instance; lookups go through `dev->cls->contracts[]`.

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
A device's capabilities are determined by its contracts (the four vtable slots above, defined by `sys_io` / `sys_power`). A device may implement more than one contract (e.g. a combined VREG + monitor chip). String form available via `sys_device_contract_type_e_to_string[]` — used by `sys_error_dev.h`'s `ERR_DEV_FEATURE_UNAVAILABLE` description (see [[SYS_ERRORS.MD]]'s `SE_describe_payload()`), which forward-declares it as an `extern` rather than including this header (breaks include ordering that early in the chain).

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
} sys_device_ops_t;
```
Any op left NULL causes the corresponding manager call to return `ERR_BASE_NOT_SUPPORTED` for that device.

### Per-Instance Error Handling

`SE_push_to_handler()` dispatches synchronously from the outermost error towards
its root cause. Device identity comes from tag payloads, regardless of owner.
See [sys_errors](../../sys_errors/SYS_ERRORS.MD) for ownership and traversal rules.

`sys_device_report_error(device_id, node)` uses that node's severity.
`sys_device_report_error_with_level()` accepts a validated explicit level; NONE
means no response. Both borrow the input and return an owned response failure.

Importance NONE ignores the device, including critical faults. During chain dispatch
it stops this device and all deeper causes, retaining diagnostics and any already
executed outer responses. For enabled devices, noncritical severity is capped by
importance; critical still invokes global shutdown and the configured device action.

`actions[5]` keeps the existing configuration packet size:

- `actions[0]`: four scope bits; bit 0 LOW, bit 1 MEDIUM, bit 2 HIGH, bit 3 CRITICAL.
  A set bit selects the dynamic action namespace; a clear bit selects static.
- `actions[1..4]`: full eight-bit IDs for LOW through CRITICAL; zero disables an action.
- Each ID is validated against its selected namespace. NULL resets all five bytes.

The former unused NONE action slot is now the scope mask. There is no static-to-dynamic
fallback. Existing static IDs with a zero first byte keep their meaning; dynamic
configurations must set the corresponding scope bit.

The application coordinator latches/stops/freezes globally once per latch episode.
Each affected device still runs its own action, including critical actions after
another device set the latch. The handler deduplicates devices within one incident;
separate incidents may run device actions again. Response failures are diagnostic-only
and preserve the failed operation's cause chain. System faults have their own hook
and never use device zero's policy. The device-manager fallback freezes all devices
when no application coordinator is linked.

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
* **`sys_device_suspend_all()`** / **`sys_device_resume_all()`** / **`sys_device_sync_all()`** / **`sys_device_reset_all()`**: Sweep the whole registry, applying the op to every eligible device and stopping at the first failure. **`sys_device_freeze_all()`** is safety-oriented: it attempts every eligible device even after a failure and returns the first error after the sweep.
* **`sys_device_uninstall_all()`**: Sweeps every populated registry slot and calls `sys_device_uninstall()` on each — same semantics as the single-device call (tolerates any device state, doesn't check ready/suspended).
* **Sweep direction follows the device-id dependency convention.** A device's `sys_io_pin_ref_t` fields (`oe_pin`, `rst_pin`, `en_pin`, ...) always point at a *lower*-id device — see `runit_board_devices.h`'s boot-order device ids. Several adapters' `suspend`/`uninstall` ops call back into that dependency's `sys_io_*` while they run (e.g. `tca6424a`'s `suspend` drives its `rst_pin` low, which is a `gpio_esp` pin). Tear-down sweeps (`suspend_all`, `uninstall_all`) therefore go **high id → low id**, so a dependent finishes its own teardown before the lower-id device it depends on goes down; bring-up sweeps (`resume_all`) go **low id → high id**, so a dependency is already active by the time a higher-id dependent resumes and touches it. `reset_all`/`freeze_all`/`sync_all` sweep low → high since none of today's adapters reach into another device's `sys_io` from those ops — if one ever does, it needs the same high→low treatment.
* **Freeze vs. suspend are orthogonal.** `freeze`/`freeze_all` never touch `state` (an adapter tracks its own `is_frozen` flag via `SYS_DEV_CTX_FREEZE`/`SYS_DEV_CTX_UNFREEZE`); only `suspend`/`suspend_all` set `state = SYS_DEV_STATE_SUSPENDED`. Consequently `resume`/`resume_all` (which only act on devices `SYS_DEV_IS_SUSPENDED`) do **not** clear a freeze that was applied without a suspend — several adapters (see e.g. `device_gpio_esp`'s `device_sync()`) clear `is_frozen` from their `sync` op instead, so lifting a freeze system-wide is `sys_device_sync_all()`, not `sys_device_resume_all()`. [[SYS_ACTIONS.MD]]'s "resume" static function calls both together for exactly this reason.
* **`sys_device_get_by_id(device_id)`**: Lock-free registry lookup; returns `NULL` if out of range or unset.
* **`sys_device_report_error(device_id, error)`**: Evaluates the supplied node's error severity against the device's `importance`. Critical errors unconditionally invoke `sys_device_app_error_policy()`. Non-critical errors are gated and clamped by `dev->importance`.
* **`sys_device_report_error_with_level(device_id, level, error)`**: Explicit severity level override variant of `sys_device_report_error()`.
* **`sys_device_app_error_policy(device_id, level, action_id, error)`**: Application-layer error policy, statically bound at link time. Avoids runtime registration and eliminates boot-order races. A weak fallback in `sys_device.c` freezes all devices on CRITICAL faults if unlinked.
* **`sys_device_set_error_handling(device_id, importance, actions)`**: Configures device importance and action IDs in one call — maps 1:1 to wire packet `0x1A` (`packet_sys_device_set_error_handling_t`). `actions` is copied into `dev->actions[5]` (IDs validated against the namespace selected by `actions[0]`), or pass `NULL` to zero it. Returns `ERR_DEV_NOT_FOUND` if `device_id` isn't registered, `ERR_INVALID_VAL_UI32` if an `actions[]` entry is out of range.

---

## Error Ownership

Every function in `sys_device.c` sets `#undef OWNER` / `#define OWNER OWNER_SYS_DEVICE_<FUNCTION>` immediately before its body, per the [[runit]] skill's module owner tagging convention — e.g. `sys_device_reset()` tags errors as `OWNER_SYS_DEVICE_RESET`. See `sys_error_dev.h` for the full owner map and device tags. Fault response failure reports `ERR_DEV_FAULT_RESPONSE_FAILED`.
