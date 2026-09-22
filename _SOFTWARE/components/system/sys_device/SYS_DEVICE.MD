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
* **`sys_device_class_t`** — everything about a device *type* that is a compile-time constant: `name`, `roles` (bitmask of `sys_device_role_e`), the feature `contracts[]` (`const void*` slots pointing at `static const` vtables, kept in flash), and the `sys_device_ops_t` lifecycle function table. Declared once per device type as a `static const`.
* **`sys_device_t`** — one *instance* in the registry: `device_id`, a pointer back to its `cls`, a manager-owned heap copy of the typed config (`cfg`/`cfg_size`), `state`, `device_handle`, and its per-instance error handling configuration (`actions[5]`, `importance` — see [Per-Instance Error Handling](#per-instance-error-handling)), and `io_locked_pins` (per-instance IO pin locks, owned by `sys_io`). Contracts are not copied onto the instance; lookups go through `dev->cls->contracts[]`.

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
typedef enum { SYS_DEVICE_CONTRACT_IO = 0, SYS_DEVICE_CONTRACT_POWER_VREG = 1, SYS_DEVICE_CONTRACT_POWER_MONITOR = 2, SYS_DEVICE_CONTRACT_POWER_USB_PD = 3, SYS_DEVICE_CONTRACT_HBRIDGE = 4, SYS_DEVICE_CONTRACT_MAX = 5 } sys_device_contract_type_e;
```
A device's capabilities are determined by its contracts (the vtable slots above, defined by `sys_io` / `sys_power` / `sys_hbridge`). Vtables are always `static const`: nothing writes to them at runtime (per-instance state such as IO pin locks lives on `sys_device_t`). A device may implement more than one contract (e.g. a combined VREG + monitor chip). String form available via `sys_device_contract_type_e_to_string[]` — used by `sys_error_dev.h`'s `ERR_DEV_FEATURE_UNAVAILABLE` description (see [[SYS_ERRORS.MD]]'s `SE_describe_payload()`), which forward-declares it as an `extern` rather than including this header (breaks include ordering that early in the chain).

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
Any op left NULL causes the corresponding single-device manager call to return `ERR_BASE_NOT_SUPPORTED` for that device (sweeps skip it).

**Every error leaving a device carries its `device_id`.** Contract calls are wrapped by the dispatchers (`SYS_DEV_DISPATCH`, `SYS_IO_DISPATCH`, `sys_power`, `sys_hbridge`), install failures by `ERR_DEV_INSTALL_FAILED`, and every lifecycle op result (single-device calls, `uninstall`, and each device in the `*_all` sweeps) by `ERR_DEV_DEP_FAILED(dev_id)` inside `sys_device.c` (`DEV_WRAP` / `DEV_RUN_OP`). Adapters need nothing beyond `SYS_DEV_CHECK_DRIVER_CALL` for this.

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
and never use device zero's policy. The device-manager fallback suspends all devices
when no application coordinator is linked.

---

## Device State Query & Dispatch Macros

* **`SYS_DEV_IS_READY(d)`** / **`SYS_DEV_IS_INSTALLED(d)`** / **`SYS_DEV_IS_SUSPENDED(d)`**: State predicates on a `sys_device_t*`.
* **`SYS_DEV_GET_CONTRACT(dev, type)`**: Fetches `dev->cls->contracts[type]`, or NULL if `dev` is NULL.
* **`SYS_DEV_FEATURE_ID(contract_type, func_name)`**: A contract function's feature id: its index in the contract struct (`offsetof / sizeof(fn ptr)`; every member is a function pointer). No per-contract feature enums. Each contract keeps a NULL-terminated `<contract>_feature_names[]` table in member order, checked against the struct by `_Static_assert`; the `ERR_DEV_FEATURE_UNAVAILABLE` logger uses these tables to name the feature.
* **`SYS_DEV_RESOLVE(device_id, contract_enum, contract_type, func_name, dev_var, contract_var)`**: The single copy of the dispatch checks. It looks the device up (`ERR_DEV_NOT_FOUND`), checks installed (`ERR_DEV_NOT_INSTALLED`) and not suspended (`ERR_DEV_SUSPENDED`), and resolves the contract and function (`ERR_DEV_FEATURE_UNAVAILABLE {dev, contract, feature}`). It declares `dev_var` / `contract_var` in the enclosing scope. Used directly where a call needs extra logic around it (`sys_io`'s pin lock, `sys_power`'s budget).
* **`SYS_DEV_DISPATCH(device_id, contract_enum, contract_type, func_name, ...)`**: `SYS_DEV_RESOLVE`, then call the function and wrap a failure with `ERR_DEV_DEP_FAILED {dev_id}`. Must be the whole body of the calling function (it always `return`s). `sys_hbridge` and `sys_power` use it as is; `sys_io` wraps it with the pin checks (`SYS_IO_DISPATCH`).

---

## Error & Failure Handling Macros

* **`SYS_DEV_CHECK_HANDLE(handle, dev_id)`**: Returns `ERR_DEV_NO_HANDLE` if `handle` is NULL.
* **`SYS_DEV_GET_ADAPTER_CONTEXT(ctx_type, hw_type, ctx_var, hw_var, input_handle)`**: Casts `input_handle` to the adapter's context type, checks it's non-NULL, then extracts and checks its `hw_handle`. The standard first line of every adapter feature function.
* **`SYS_DEV_TRY(err_ptr, ctx)`**: Wraps a nested `err_h` failure (for example a `sys_io_*` call on a dependency pin) as `ERR_DEV_DEP_FAILED` carrying `ctx`'s `dev_id`, then returns it.

**No logging in drivers or adapters.** Drivers and adapters don't call `ESP_LOG*`. All failure context travels in the error chain: device ID, adapter line, ESP code, and value/min/max for range checks (`SE_CHECK_IN_RANGE` at the adapter boundary, before calling the driver). There are no exceptions.
* **`SYS_DEV_CHECK_DRIVER_CALL(driver_call, ctx)`**: Runs a raw driver call (`esp_err_t`). A failure returns `ERR_DEV_DRIVER_FAILED {dev_id, line}` over `ERR_ESP_ERR {code}`: the device, the exact adapter call site (the node owner names the device type) and the ESP code. This is the default way to call a `driver_xxx.c` function from an adapter.
* **`SYS_DEV_DRIVER_ERR(esp_err, ctx)`**: Builds, but does not return, the same `ERR_DEV_DRIVER_FAILED` over `ERR_ESP_ERR` chain for an `esp_err_t` obtained elsewhere: an ESP-IDF call made directly by an adapter (`device_gpio_esp`), or a failure reported from a driver task through a callback (`device_ap33772s` AVS keep-alive).
* **`SYS_DEV_INSTALL_FAIL(err, device_id, out_handle, uninstall_fn, ctx)`**: Called once from the `fail:` label of an `install` implementation after `err` is set. NULLs `*out_handle`, and if `ctx` was allocated, suspends error reporting (teardown noise would bury the real cause) and rolls back via `uninstall_fn(ctx)`, then returns `err`. `sys_device_install_cfg()` adds `ERR_DEV_INSTALL_FAILED {dev_id}` on top. `uninstall_fn` must tolerate any subset of `ctx`'s fields being unset, and must never early-return on a failing teardown step — it has to free everything regardless.

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
* **`SYS_DEV_INSTALL_STEP(expr, what)`**: Runs one install step; on error, wraps it as `ERR_DEV_INSTALL_STEP_FAILED {line}` (the adapter call site) and `goto fail`. `what` is a source-level label only. Requires `err_h err` and a `fail:` label in scope.
* **`SYS_DEV_TEARDOWN_STEP(err_acc, expr)`**: Runs one teardown step, keeping only the *first* error seen in `err_acc`. Never early-returns — every subsequent teardown step still runs even if an earlier one failed.
* **`SYS_DEVICE_CREATE(cls_ptr, cfg_ptr)`**: Shorthand for `sys_device_install_cfg(cls_ptr, cfg_ptr->device_id, cfg_ptr, sizeof(*cfg_ptr))`. Requires `cfg_ptr`'s first member to be `uint8_t device_id`. This is the standard way a `d_xxx_create()` function installs its device.

---

## Manager API Functions

* **`sys_device_install_cfg(cls, device_id, cfg, cfg_size)`**: Heap-copies `cfg`, sets `cls` on the instance, marks it `INSTALLING`, then runs `cls->ops.install`. On failure the registry slot and heap copy are freed and the error is returned; on success state becomes `INSTALLED`. Prefer `SYS_DEVICE_CREATE()`.
* **`sys_device_uninstall(device_id)`**: Removes the registry entry, runs the device's `uninstall` op, then frees the config copy and the instance. Config is freed only *after* the adapter has torn down, since it may still read its own copy of the config until then.
* **`sys_device_reset(device_id)`** / **`sys_device_suspend(device_id)`** / **`sys_device_resume(device_id)`** / **`sys_device_freeze(device_id)`** / **`sys_device_sync(device_id)`**: Single-device lifecycle calls; each checks found/installed/not-suspended (where relevant) before invoking the corresponding op, and returns `ERR_BASE_NOT_SUPPORTED` if the device doesn't implement it. `suspend`/`resume` no-op (return `NULL`) if the device is already in the target state.
* **`sys_device_suspend_all()`** / **`sys_device_resume_all()`** / **`sys_device_sync_all()`** / **`sys_device_reset_all()`**: Sweep the whole registry, applying the op to every eligible device. All sweeps (including `sys_device_freeze_all()` and `sys_device_uninstall_all()`) are **best effort**: a failing device doesn't stop the sweep, the first failure (wrapped with its `device_id`) is returned, and later failures are sent to diagnostics (`SE_send`) and released. `suspend_all` is the fault safe-state path, so it must reach every device.
* **`sys_device_uninstall_all()`**: Sweeps every populated registry slot and calls `sys_device_uninstall()` on each — same semantics as the single-device call (tolerates any device state, doesn't check ready/suspended).

### Onboard devices and user entry points

* **Onboard devices** are baked onto the PCB and installed by the board config (`runit_board_devices_init`). Right after each install it calls **`sys_device_set_onboard(id)`** (through `RUNIT_BOARD_DEVICE(id, create_call)`), which sets `sys_device_t.onboard`. The flag lives with the instance: a system uninstall (hard reset, `uninstall_all`) drops it, and the board's reinstall sets it again.
* **User entry points** are what inbound packets and replayed recorded actions call. **`sys_device_user_uninstall(id)`** returns `ERR_DEV_ONBOARD {dev_id}` for an onboard device, otherwise it's `sys_device_uninstall()`. **`sys_device_user_uninstall_all()`** uninstalls every non-onboard device (same order, best effort). Because an onboard ID stays occupied, a user install on it fails with `ERR_DEV_ALREADY_EXIST`, so onboard devices can't be replaced either.
* That's the only firmware rule. Everything else users may do with onboard devices (contracts, reset, suspend, error handling) is app policy, enforced in the client.
* System paths (boot action, static actions such as hard reset, fault handling) keep using the plain functions.
* **Sweep direction follows the device-id dependency convention.** A device's `sys_io_pin_ref_t` fields (`oe_pin`, `rst_pin`, `en_pin`, ...) always point at a *lower*-id device — see `runit_board_cfg.c`'s boot-order device ids. Several adapters' `suspend`/`uninstall` ops call back into that dependency's `sys_io_*` while they run (e.g. `tca6424a`'s `suspend` drives its `rst_pin` low, which is a `gpio_esp` pin). Tear-down sweeps (`suspend_all`, `uninstall_all`) therefore go **high id → low id**, so a dependent finishes its own teardown before the lower-id device it depends on goes down; bring-up sweeps (`resume_all`) go **low id → high id**, so a dependency is already active by the time a higher-id dependent resumes and touches it. `reset_all`/`freeze_all`/`sync_all` sweep low → high since none of today's adapters reach into another device's `sys_io` from those ops — if one ever does, it needs the same high→low treatment.
* **Faults use suspend, not freeze.** Suspend is the safe state: each adapter's `suspend` puts its outputs into a safe condition (PWM off via OE + sleep, expander in reset, regulator output off + EN low, PD output off, DAC powered down, H-bridges coast) and the device state becomes `SUSPENDED`, so dispatch rejects new writes (`ERR_DEV_SUSPENDED`) instead of queueing them. Critical faults (`runit_system_fault`, `runit_enter_safe_state`, and this component's weak fallbacks) call `sys_device_suspend_all()`. Exception still open: `device_gpio_esp`'s suspend aliases its freeze, so ESP GPIO outputs keep their level.
* **Freeze vs. suspend are orthogonal.** `freeze`/`freeze_all` never touch `state` (an adapter tracks its own `is_frozen` flag via `SYS_DEV_CTX_FREEZE`/`SYS_DEV_CTX_UNFREEZE`); only `suspend`/`suspend_all` set `state = SYS_DEV_STATE_SUSPENDED`. Consequently `resume`/`resume_all` (which only act on devices `SYS_DEV_IS_SUSPENDED`) do **not** clear a freeze that was applied without a suspend — several adapters (see e.g. `device_gpio_esp`'s `device_sync()`) clear `is_frozen` from their `sync` op instead, so lifting a freeze system-wide is `sys_device_sync_all()`, not `sys_device_resume_all()`. [[SYS_ACTIONS.MD]]'s "resume" static function calls both together for exactly this reason.
* **`sys_device_get_by_id(device_id)`**: Lock-free registry lookup; returns `NULL` if out of range or unset.
* **`sys_device_report_error(device_id, error)`**: Evaluates the supplied node's error severity against the device's `importance`. Critical errors unconditionally invoke `sys_device_app_error_policy()`. Non-critical errors are gated and clamped by `dev->importance`.
* **`sys_device_report_error_with_level(device_id, level, error)`**: Explicit severity level override variant of `sys_device_report_error()`.
* **`sys_device_register_error_policy(fn)`**: The application registers its error policy (`sys_device_error_policy_f`: device, level, action scope/id, error). runit does it in `runit_error_wiring_init()`, the first boot step. Without one, a CRITICAL device error suspends all devices.
* **`sys_device_set_error_handling(device_id, importance, actions)`**: Configures device importance and action IDs in one call — maps 1:1 to wire packet `0x1A` (`packet_sys_device_set_error_handling_t`). `actions` is copied into `dev->actions[5]` (IDs validated against the namespace selected by `actions[0]`), or pass `NULL` to zero it. Returns `ERR_DEV_NOT_FOUND` if `device_id` isn't registered, `ERR_INVALID_VAL_UI32` if an `actions[]` entry is out of range.

---

## Error Ownership

Every function in `sys_device.c` sets `#undef OWNER` / `#define OWNER OWNER_SYS_DEVICE_<FUNCTION>` immediately before its body, per the owner tagging convention (runit-esp skill, errors.md) — e.g. `sys_device_reset()` tags errors as `OWNER_SYS_DEVICE_RESET`. See `sys_error_dev.h` for the full owner map and device tags. Fault response failure reports `ERR_DEV_FAULT_RESPONSE_FAILED`.
