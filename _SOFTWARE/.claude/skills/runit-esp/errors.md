# Error handling

Full design: [components/sys_errors/SYS_ERRORS.MD](../../../components/sys_errors/SYS_ERRORS.MD). API: `components/sys_errors/include/sys_error.h`, hooks: `include/sys_error_hooks.h`. Who registers what (router, policy, hooks): [architecture.md](architecture.md) §4.1.

## 1. Conventions

- **Return `err_h`** from any function that can fail; `NULL` = success. `void` functions handle errors locally.
- **Hide vendor/ESP-IDF codes** from upper layers: wrap `esp_err_t` with `SE_CONVERT_ESP` / `SE_TRY_ESP`. A device's chip access returns `err_h` (`sys_i2c_*` already does); an ESP-IDF call made directly uses `SE_CONVERT_ESP` / `SE_TRY_ESP`.
- **Macros:**

  | Macro | Does |
  |---|---|
  | `SE_FAIL(TAG, …)` | Return a new error |
  | `SE_TRY(call)` | Propagate; adds an `ERR_DEP_FAILED` hop, so the trace is kept |
  | `SE_TRY_WRAP(call, TAG, …)` | Propagate with context |
  | `SE_TRY_ESP(esp_call)` | Propagate an `esp_err_t` |
  | `SE_REPORT(call)` | Consume: responses now, logging later |
  | `SE_RAISE(TAG, …)` | New error and report it (for errors that aren't returned) |
  | `SE_log(err)` | Diagnostics only (queued) |
  | `SYS_DEV_TRY(call, ctx)` / `SYS_DEV_WRAP(err, id)` | Propagate with the device ID (`WRAP` passes `NULL` through) |
  | `SE_WRAP_DEV_ERR(err, dev_id)` | Attribute to a device at a contract boundary |

  `_OWNED` variants take an explicit owner. Report at the origin (top-level entry points, task loops) with `SE_REPORT`.
- **Severity (`se_level_e`: NONE / LOW / MEDIUM / HIGH / CRITICAL) follows the consequence.**
  - CRITICAL means the whole system must stop: VM stop + suspend all. Only real system faults use it (`BLE_HARDWARE_FAULT`, `DEV_FAULT_RESPONSE_FAILED`, `VM_EXEC_FAULT_LATCHED`). It bypasses the per-device importance threshold, except NONE: NONE ignores every error of that device, critical included (the user's opt-out for a device under test; devices start at LOW). With no device attributed it goes to the registered system hook.
  - Bad arguments, missing handles, out of memory and install failures are HIGH.
  - Pool exhaustion (`ERR_BASE_POOL_EXHAUSTED`) is LOW.
- **New tags:** add them to the module's `SYS_ERROR_<MODULE>_MAP` with an explicit ID in the module's domain, `X(ERR_<MODULE>_<WHAT>, 0xDDnn, level, struct)`, plus a `LOG_BODY_<tag>` in its `LOGGER_MAP`. Never renumber existing ones. Tags that carry a device ID must be added to the module's `SYS_ERROR_<MODULE>_DEVICE_TAGS(X)` (next to its tags; `device_id_of()` in `sys_error_handler.c` reads the lists) to take part in device policy.
- **Owner tag:** every `.c` that raises errors starts with `#define OWNER OWNER_<MODULE>`, listed in the module's `errors/sys_error_<module>.h` owner map (layout: SYS_ERRORS.MD "Error maps and aggregation"). Files that raise nothing define no owner; `vm_errors.c` passes owners explicitly (`_OWNED`).
- **Never drop an `err_h`.** Every `err_h` function is `SE_MUST_USE` (a dropped result fails the build). An intentional drop is `SE_release(call)`; don't use a call as a condition (`if (call() != NULL)` leaks the chain).
- **Steps that must all run** (teardown, suspend, per-channel sweeps, fault notification) accumulate with `SYS_DEV_TEARDOWN_STEP` and return the first error (an ESP call: `SYS_DEV_TEARDOWN_STEP(err, SE_CONVERT_ESP(call))`).
- **Ownership:** `SE_push_to_handler` / `SE_REPORT` **consume** the chain. `SE_send`, logging and hooks **borrow**. Anything else you inspect or discard needs `SE_release(err)`. Never share a cause between two chains. The pool has 32 slots, so a leaked node reduces capacity for good.
- **Repeated chains** (same tag and owner at every node) within `CONFIG_SYS_ERRORS_REPEAT_WINDOW_MS` (2 s) are counted, not logged or sent; the window's end logs "… repeated N more time(s)". Responses still run for every occurrence.
- **No logging in devices.** Put the context into the error: the device ID (added by the dispatchers, `SYS_DEV_WRAP` for event listeners and tasks), value/min/max (`SE_CHECK_IN_RANGE`), and the ESP code.
- **Device errors carry the device ID**: wrapped by the contract dispatchers, by install, and by `sys_device` for lifecycle ops and sweeps ([devices.md](devices.md) §5.4). An event listener or task of a device has no dispatcher, so it wraps its own error: `return SYS_DEV_WRAP(err, SYS_DEV_GET_ID(c));`.
- **Device owner:** device files define `OWNER OWNER_DEVICE` (one owner for all). Severity for the device policy comes from the real failure, not from the `ERR_DEV_DEP_FAILED` wrapper (SYS_ERRORS.MD, dispatch policy).
- **Domain hooks:** add one only when a module has real containment, and register it from runit.
- **Static allocation:** use the `R_*_DEFINE` / `R_TASK_START` macros from `components/utils/utils.h` instead of `xQueueCreate`, `xTaskCreate` etc. This removes the out-of-memory error path entirely.

## 2. Open

- **H-bridge path (DRV8962 / sys_hbridge):** no hbridge error map (overcurrent, nFAULT, thermal, stall); the over-current guard of `get_current_mA` still returns `ERR_BASE_INVALID_STATE, 0`; no hbridge tag carries `dev_id` in the device tags. Faults themselves are published as HBRIDGE events.
- **Containment hooks** where a module needs one (i2c bus recovery).
- SYS_ERRORS.MD references a host test `tools/tests/test_sys_errors.py` that no longer exists.
