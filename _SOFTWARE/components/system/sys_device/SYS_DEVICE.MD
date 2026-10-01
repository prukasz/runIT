# System Device Manager (sys_device)

IMPORTANT: If you modify the source files in this component (sys_device.h or sys_device.c), you must update this documentation to reflect the changes.

## Overview
The `sys_device` component is the central registry and lifecycle manager for every hardware device instance in runIT. Devices describe themselves declaratively as a `sys_device_class_t` (type id, cfg size, pin refs, name, feature contracts, lifecycle ops, own operations) and hand a typed config struct to `sys_device_create()` (via the `SYS_DEVICE_CREATE()` wrapper); `sys_device` owns the registry slot and dispatch of every subsequent lifecycle/feature call by `device_id`.

### Directory Map
```
components/system/sys_device/
├── CMakeLists.txt
├── SYS_DEVICE.MD         # This documentation
├── DEVICE_CLASS.MD       # Design: the class carries the type id, cfg size and pin refs
├── sys_device.c          # Registry, create/install/uninstall, router, dispatch, suspend/resume
├── include/
│   └── sys_device.h      # Contracts, class/instance structs, create, dispatch & context-and-step macros
└── errors/
    └── sys_error_dev.h   # sys_device owner map and ERR_DEV_* error tag map
```

---

## Architecture & Concept

### Class vs. Instance
* **`sys_device_class_t`** — everything about a device *type* that is a compile-time constant: `type_id`, `cfg_size`, the `pin_ref_offsets` of its cfg, `name`, the feature `contracts[]` (`const void*` slots pointing at `static const` vtables, kept in flash), the `sys_device_ops_t` lifecycle function table and the device's own operations (`dev_ops`). Declared once per device type as a public `const g_<chip>_class` and registered with `sys_device_register_class()`.
* **`sys_device_t`** — one *instance* in the registry: `device_id`, a pointer back to its `cls`, `state`, `device_handle`, and its per-instance error handling configuration (`actions[5]`, `importance` — see [Per-Instance Error Handling](#per-instance-error-handling)), and `io_locked_pins` (per-instance IO pin locks, owned by `sys_io`). Contracts are not copied onto the instance; lookups go through `dev->cls->contracts[]`.

Contracts are declarative: `sys_device` sets `cls` on the instance before running `cls->ops.install` — a device never calls something like `sys_io_register_driver()` / `sys_power_register_*()` itself.

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
A device's capabilities are determined by its contracts (the vtable slots above, defined by `sys_io` / `sys_power` / `sys_hbridge`). Vtables are always `static const`: nothing writes to them at runtime (per-instance state such as IO pin locks lives on `sys_device_t`). A device may implement more than one contract (e.g. a combined VREG + monitor chip). **Contracts name themselves.** The enum is the stable id of a contract (slot in `cls->contracts[]`, published to the app), but the names come from the contract modules: each keeps its NULL-terminated `<contract>_feature_names[]` (vtable member order) and runit registers them once in `runit_error_wiring_init()` with `sys_device_register_contract(id, name, feature_names)`. `sys_device_contract_name()` / `sys_device_feature_name()` read the registry; `sys_error_dev.h`'s `ERR_DEV_FEATURE_UNAVAILABLE` description forward-declares just those two functions (it is parsed before `err_h` exists, so it can't include this header). Adding a contract: one enum entry, the module's names table, one register line.

### Lifecycle Ops
```c
typedef struct sys_device_ops_t {
  err_h (*install)(const void* cfg, void** out_device_handle);
  err_h (*uninstall)(void* device_handle);
  err_h (*reset)(void* device_handle);
  err_h (*suspend)(void* device_handle);
  err_h (*resume)(void* device_handle);
} sys_device_ops_t;
```
Any op left NULL causes the corresponding single-device manager call to return `ERR_BASE_NOT_SUPPORTED` for that device (sweeps skip it).

**Every error leaving a device carries its `device_id`.** Contract calls are wrapped by the dispatchers (`SYS_DEV_DISPATCH`, `SYS_IO_DISPATCH`, `sys_power`, `sys_hbridge`), install failures by `ERR_DEV_INSTALL_FAILED`, and every lifecycle op result (single-device calls, `uninstall`, and each device in the `*_all` sweeps) by `ERR_DEV_DEP_FAILED(dev_id)` inside `sys_device.c` (`SYS_DEV_WRAP` / `DEV_RUN_OP`). Devices need nothing for this; an event listener or task of a device wraps its own error with `SYS_DEV_WRAP`. For the device policy, `ERR_DEV_DEP_FAILED` is only attribution: its severity is the one of the real failure below it (SYS_ERRORS.MD).

### Per-Instance Error Handling

`SE_push_to_handler()` dispatches synchronously from the outermost error towards
its root cause. Device identity comes from tag payloads, regardless of owner.
See [sys_errors](../../sys_errors/SYS_ERRORS.MD) for ownership and traversal rules.

`sys_device_report_error(device_id, node)` uses that node's severity.
`sys_device_report_error_with_level()` accepts a validated explicit level; NONE
means no response. Both borrow the input and return an owned response failure.

A device starts at importance LOW: it handles its critical errors (global shutdown
and the device's Critical action) and ignores the rest. Importance NONE is the
user's explicit opt-out: it ignores every error of the device, critical ones
included (a device under test; the app warns). During chain dispatch it stops all
handling of this device and all deeper causes, retaining diagnostics (the chain
is still logged) and any already executed outer responses. From LOW up,
importance selects which severities reach the device policy; admitted errors
keep their original severity, and a critical error always goes through.

`actions[5]` keeps the existing configuration packet size:

- `actions[0]`: four scope bits; bit 0 LOW, bit 1 MEDIUM, bit 2 HIGH, bit 3 CRITICAL.
  A set bit selects the dynamic action namespace; a clear bit selects static.
- `actions[1..4]`: full eight-bit IDs for LOW through CRITICAL; zero disables an action.
- Each ID is validated against its selected namespace: static `0..CONFIG_SYS_ACTIONS_STATIC_SLOTS-1`,
  dynamic `0..255` (the range sys_actions records and runs). NULL resets all five bytes.

Which levels' actions can run under an importance: NONE → none; LOW → Critical;
MEDIUM → High, Critical; HIGH → Medium, High, Critical; CRITICAL → all four.

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

* **`SYS_DEV_IS_READY(d)`** (installed, not suspended) / **`SYS_DEV_IS_SUSPENDED(d)`**: State predicates on a `sys_device_t*`. "Installed" (`state >= INSTALLED`) is tested in one place, `sys_device_check()`.
* **`sys_device_check(dev, device_id, allow_suspended)`** / **`SYS_DEV_REQUIRE(dev, device_id, allow_suspended)`**: The one found / installed / not-suspended check (`ERR_DEV_NOT_FOUND`, `ERR_DEV_NOT_INSTALLED`, `ERR_DEV_SUSPENDED`), as a function and as a guard that returns its error. `SYS_DEV_RESOLVE`, the lifecycle calls and the router use it; the suspend / resume toggles and the router pass `allow_suspended = true` and test the suspended state themselves.
* **`SYS_DEV_GET_CONTRACT(dev, type)`**: Fetches `dev->cls->contracts[type]`, or NULL if `dev` is NULL.
* **`SYS_DEV_FEATURE_ID(contract_type, func_name)`**: A contract function's feature id: its index in the contract struct (`offsetof / sizeof(fn ptr)`; every member is a function pointer). No per-contract feature enums. Each contract keeps a NULL-terminated `<contract>_feature_names[]` table in member order, checked against the struct by `_Static_assert`; the `ERR_DEV_FEATURE_UNAVAILABLE` logger uses these tables to name the feature.
* **`SYS_DEV_RESOLVE(device_id, contract_enum, contract_type, func_name, dev_var, contract_var)`**: The single copy of the dispatch checks. It looks the device up (`ERR_DEV_NOT_FOUND`), checks installed (`ERR_DEV_NOT_INSTALLED`) and not suspended (`ERR_DEV_SUSPENDED`), and resolves the contract and function (`ERR_DEV_FEATURE_UNAVAILABLE {dev, contract, feature}`). It declares `dev_var` / `contract_var` in the enclosing scope. Used directly where a call needs extra logic around it (`sys_io`'s pin lock, `sys_power`'s budget).
* **`SYS_DEV_DISPATCH(device_id, contract_enum, contract_type, func_name, ...)`**: `SYS_DEV_RESOLVE`, then call the function and wrap a failure with `ERR_DEV_DEP_FAILED {dev_id}`. Must be the whole body of the calling function (it always `return`s). `sys_hbridge` and `sys_power` use it as is; `sys_io` wraps it with the pin checks (`SYS_IO_DISPATCH`).

---

## Error & Failure Handling Macros

* **`SYS_DEV_WRAP(err_expr, dev_id)`**: Wraps a result as `ERR_DEV_DEP_FAILED {dev_id}` so device policy can attribute it; `NULL` passes through. One wrap for the dispatchers, the lifecycle ops (`sys_device.c`) and the event listeners of the devices (no dispatcher wraps those: `return SYS_DEV_WRAP(handle_x(event, c), SYS_DEV_GET_ID(c));`).
* **`SYS_DEV_TRY(err_expr, ctx)`**: `return SYS_DEV_WRAP(err, SYS_DEV_GET_ID(ctx))` when the call failed: for a call on a dependency (a `sys_io_*` call on a pin of another device).

**No logging in devices.** A device doesn't call `ESP_LOG*`. All failure context travels in the error chain: device ID, ESP code, and value/min/max for range checks (`SE_CHECK_IN_RANGE` at the device boundary, before calling the driver). There are no exceptions.
* **`SYS_DEV_INSTALL_FAIL(err, out_handle, uninstall_fn, ctx)`**: Called once from the `fail:` label of an `install` implementation after `err` is set. NULLs `*out_handle`, and if `ctx` was allocated, suspends error reporting (teardown noise would bury the real cause) and rolls back via `uninstall_fn(ctx)`, then returns `err`. `sys_device_install()` adds `ERR_DEV_INSTALL_FAILED {dev_id}` on top. `uninstall_fn` must tolerate any subset of `ctx`'s fields being unset, and must never early-return on a failing teardown step — it has to free everything regardless.

---

## Device Context & Installation Step Helpers

A device's context embeds a `sys_device_base_t base` as its first member (`SYS_DEV_CTX_FROM` casts the handle to the context, `SYS_DEV_STEP_DONE` records install steps in `base`):
```c
typedef struct {
  uint8_t device_id;
  uint16_t steps_done; /* bitmask of completed install steps */
} sys_device_base_t;
```

* **`SYS_DEV_STEP_DONE(ctx, bit)`** / **`IF_SYS_DEV_STEP_DONE(ctx, bit)`**: Set/test a bit in `steps_done`, so teardown can roll back exactly what was built instead of inferring it from field sentinels.
* **`SYS_DEV_INSTALL_STEP(expr, what)`**: Runs one install step; on error, wraps it as `ERR_DEV_INSTALL_STEP_FAILED {line}` (the call site in the device) and `goto fail`. `what` is a source-level label only. Requires `err_h err` and a `fail:` label in scope.
* **`SYS_DEV_INSTALL_STEP_BIT(ctx, bit, expr, what)`**: `SYS_DEV_INSTALL_STEP`, then `SYS_DEV_STEP_DONE(ctx, bit)` once the step succeeded: the usual pair in one line.
* **`SYS_DEV_TEARDOWN_STEP(err_acc, expr)`**: Runs one teardown step, keeping only the *first* error seen in `err_acc`. Never early-returns — every subsequent teardown step still runs even if an earlier one failed.
* **`SYS_DEVICE_CREATE(cls_ptr, cfg_ptr)`**: Shorthand for `sys_device_create(cls_ptr, cfg_ptr, sizeof(*cfg_ptr))`: the board's way to create a device of a class. `cfg_ptr`'s first member must be `uint8_t device_id`.

---

## Manager API Functions

* **`sys_device_uninstall(device_id)`**: Removes the registry entry, runs the device's `uninstall` op, then frees the config copy and the instance. Config is freed only *after* the device has torn down, since it may still read its own copy of the config until then.
* **`sys_device_reset(device_id)`** / **`sys_device_suspend(device_id)`** / **`sys_device_resume(device_id)`**: Single-device lifecycle calls; each checks found/installed/not-suspended (where relevant) before invoking the corresponding op, and returns `ERR_BASE_NOT_SUPPORTED` if the device doesn't implement it. `suspend`/`resume` no-op (return `NULL`) if the device is already in the target state. A `suspend` that fails still leaves the device `SUSPENDED` (the error is returned): a half-made-safe device must not look active, and `resume` restores whatever was made safe. A failing `resume` leaves it suspended.
* **`sys_device_suspend_all()`** / **`sys_device_resume_all()`** / **`sys_device_reset_all()`**: Sweep the whole registry, applying the op to every eligible device. All sweeps (including `sys_device_uninstall_all()`) are **best effort**: a failing device doesn't stop the sweep, the first failure (wrapped with its `device_id`) is returned, and later failures are sent to diagnostics (`SE_send`) and released. `suspend_all` is the fault safe-state path, so it must reach every device.
* **`sys_device_uninstall_all()`**: Sweeps every populated registry slot and calls `sys_device_uninstall()` on each — same semantics as the single-device call (tolerates any device state, doesn't check ready/suspended).

### Onboard devices and user entry points

* **Onboard devices** are baked onto the PCB and installed by the board config (`runit_board_devices_init`). It creates each one with **`SYS_DEVICE_CREATE_ONBOARD(&g_<chip>_class, &cfg)`** (`sys_device_create_onboard()`: `sys_device_create()`, then `sys_device_t.onboard` is set). The flag lives with the instance: a system uninstall (hard reset, `uninstall_all`) drops it, and the board's reinstall sets it again.
* **User entry points** are what inbound packets and replayed recorded actions call. **`sys_device_user_uninstall(id)`** returns `ERR_DEV_ONBOARD {dev_id}` for an onboard device, otherwise it's `sys_device_uninstall()`. **`sys_device_user_uninstall_all()`** uninstalls every non-onboard device (same order, best effort). Because an onboard ID stays occupied, a user install on it fails with `ERR_DEV_ALREADY_EXIST`, so onboard devices can't be replaced either.
* That's the only firmware rule. Everything else users may do with onboard devices (contracts, reset, suspend, error handling) is app policy, enforced in the client.
* System paths (boot action, static actions such as hard reset, fault handling) keep using the plain functions.
* **Sweep direction follows the device-id dependency convention.** A device's `sys_io_pin_ref_t` fields (`oe_pin`, `rst_pin`, `en_pin`, ...) always point at a *lower*-id device — see `runit_board_cfg.c`'s boot-order device ids. Several devices' `suspend`/`uninstall` ops call back into that dependency's `sys_io_*` while they run (e.g. `tca6424a`'s `suspend` drives its `rst_pin` low, which is a `gpio_esp` pin). Tear-down sweeps (`suspend_all`, `uninstall_all`) therefore go **high id → low id**, so a dependent finishes its own teardown before the lower-id device it depends on goes down; bring-up sweeps (`resume_all`) go **low id → high id**, so a dependency is already active by the time a higher-id dependent resumes and touches it. `reset_all` sweeps low → high since none of today's devices reach into another device's `sys_io` from those ops — if one ever does, it needs the same high→low treatment. User installs enforce the convention in `sys_device_create` (the class's `SYS_DEVICE_PINS`): a pin ref on the same or a higher ID is refused with `ERR_DEV_PIN_ORDER` (dev_id, pin_dev_id, pin) before the device is created.
* **Faults use suspend.** Suspend is the safe state: each device's `suspend` puts its outputs into a safe condition (PWM off via OE + sleep, expander in reset, regulator output off + EN low, PD output off, DAC powered down, H-bridges coast) and the device state becomes `SUSPENDED`, so dispatch rejects new writes (`ERR_DEV_SUSPENDED`) instead of queueing them. Critical faults (`runit_system_fault`, `runit_enter_safe_state`, and this component's weak fallbacks) call `sys_device_suspend_all()`. `device_gpio_esp`: suspend sets PWM duty 0, push-pull outputs low and open-drain outputs released on the pins no other device locked, and resume restores them.
* **`sys_device_get_by_id(device_id)`**: Lock-free registry lookup; returns `NULL` if out of range or unset.
* **`sys_device_report_error(device_id, error)`**: Evaluates the supplied node's error severity against the device's `importance`. Critical errors invoke the registered error policy at every importance but NONE, which ignores every error. Non-critical errors below the importance threshold are ignored; admitted errors keep their original severity.
* **`sys_device_report_error_with_level(device_id, level, error)`**: Explicit severity level override variant of `sys_device_report_error()`.
* **`sys_device_register_error_policy(fn)`**: The application registers its error policy (`sys_device_error_policy_f`: device, level, action scope/id, error). runit does it in `runit_error_wiring_init()`, the first boot step. Without one, a CRITICAL device error suspends all devices.
* **`sys_device_set_error_handling(device_id, importance, actions)`**: Configures device importance and action IDs in one call — maps 1:1 to wire packet `0x1A` (`packet_sys_device_set_error_handling_t`). `actions` is copied into `dev->actions[5]` (IDs validated against the namespace selected by `actions[0]`), or pass `NULL` to zero it. Returns `ERR_DEV_NOT_FOUND` if `device_id` isn't registered, `ERR_INVALID_VAL_UI32` if an `actions[]` entry is out of range.

---

## Packet Router

`sys_device_route(frame, len, reply)` handles the headers of class `0x01` that the fixed system packets (uninstall, io, power, ...) don't own. `dec_sys_contracts_decode()` hands it every header that is `0x00` or `>= 0x80`; anything else in between is still `ERR_INTERFACE_UNKNOWN_PACKET`. A getter's bytes go to `sys_interface_respond()` (`sys_device` knows no transport: it fills the caller's `sys_device_reply_t`).

```
[0x00][type_id][cfg...]             create: cfg is the wire struct, cfg[0] = device_id
[0x80..0xFF][device_id][args...]    an operation of the device itself
```

* **Create.** A device type is its class (`g_<chip>_class`, public const data): `type_id`, `cfg_size`, `pin_ref_offsets` / `pin_ref_count` (`SYS_DEVICE_PINS(table)`, the `offsetof` of each `sys_io_pin_ref_t` in the cfg) next to the name, contracts, ops and `dev_ops`. runit registers it once in `devices_register_classes()` (`devices` component, a runit boot step): `sys_device_register_class(&g_<chip>_class)`; the table is `SYS_DEVICE_MAX_CLASSES` (16), read-only after boot. The router finds the class by `type_id` (`ERR_DEV_TYPE_UNKNOWN`) and calls `sys_device_create(cls, frame + 2, len - 2)`, which checks the exact size (`ERR_DEV_CREATE_SIZE`), every pin ref is unused or on a lower-ID device (`ERR_DEV_PIN_ORDER`), then `sys_device_install`. The board uses `SYS_DEVICE_CREATE(&g_<chip>_class, &(d_<chip>_cfg_t){...})`, which passes `sizeof` of the cfg. Chip-specific range checks (the servo's `min_us <= home_us <= max_us`) are in the class's `install`, so they come wrapped in `ERR_DEV_INSTALL_FAILED`. The cfg struct (`__packed`, device_id first, `sys_io_pin_ref_t` pins) and the `//#device` / `//#contract` annotations live in the device's own header (`device_<chip>.h`; `@type-id` is the type byte, `<CHIP>_TYPE_ID` the same number for the class), so such a device has no `d_<chip>_create`, no `dec_device_<chip>.h` and no 0x40..0x48 install packet. DRV8962 is the exception: it is not on the board and still uses `d_drv8962_create` over the unchecked `sys_device_install`. Design: [DEVICE_CLASS.MD](DEVICE_CLASS.MD).
* **Operations.** A class lists its own operations (`dev_ops` / `dev_ops_count`, set with `SYS_DEVICE_OPS(table)`; one row = `SYS_DEVICE_OP(code, arg_type, handler)` or `SYS_DEVICE_OP_RO` for a getter). One table per device, in the file with the class. The handler is `err_h (void* device_handle, const uint8_t* args, size_t len, sys_device_reply_t* reply)`; read the arguments with `SYS_DEVICE_OP_ARGS(type, name, args)` (copy, no alignment assumption) and return data with `sys_device_reply_put()`.
* **Checks before the handler:** device found (`ERR_DEV_NOT_FOUND`), installed (`ERR_DEV_NOT_INSTALLED`), not suspended unless the op is read-only (`ERR_DEV_SUSPENDED`), op known (`ERR_DEV_OP_UNKNOWN`) and the argument size exact (`ERR_DEV_OP_SIZE`). The handler's own error is wrapped with `ERR_DEV_DEP_FAILED {dev_id}` like every lifecycle op.
* **Rejections aren't faults.** `ERR_DEV_OP_UNKNOWN / OP_SIZE / TYPE_UNKNOWN / CREATE_SIZE` are rejected commands: they are not attributed to a device by `device_id_of()` (like `ERR_DEV_ONBOARD`).
* **Rules for an op:** never block (set a target, let the chip or a task move); change IO / power outputs only through the contracts (pin locks, budget, events live there); an operation two device types both need becomes a contract function.

## Error Ownership

Every function in `sys_device.c` sets `#undef OWNER` / `#define OWNER OWNER_SYS_DEVICE_<FUNCTION>` immediately before its body, per the owner tagging convention (runit-esp skill, errors.md) — e.g. `sys_device_reset()` tags errors as `OWNER_SYS_DEVICE_RESET`. See `sys_error_dev.h` for the full owner map and device tags. Fault response failure reports `ERR_DEV_FAULT_RESPONSE_FAILED`.
