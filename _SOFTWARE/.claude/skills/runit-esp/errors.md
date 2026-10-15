# Error handling

The design and every macro: [ERRORS.MD](../../../components/core/errors/ERRORS.MD) and [core/errors/AGENTS.md](../../../components/core/errors/AGENTS.md). Who responds to errors: SYS_DEVICE.MD "Error policy". How a reported chain leaves the board: ROUTER.MD "Error record". This file is the checklist that applies in every component.

## Rules

- **Return `err_h`** from anything that can fail; `NULL` = success, never `NULL` for a failure. Every such function is `MUST_USE`.
- **Create and propagate** with the `ERR_*` macros (`ERR_NEW`, `ERR_WRAP`, `ERR_TRY`, `ERR_TRY_WRAP`, `ERR_CHECK`, `ERR_ESP` / `ERR_TRY_ESP` for `esp_err_t`). Vendor and ESP-IDF codes never pass up raw.
- **End a chain once:** `err_report` where nobody returns it any more (task loops, callbacks, boot steps); `err_free` when it is handled; `err_log` only borrows. Code on the reporting path (the connector task, the log hook, the sink) never reports: `err_log` + `err_free`. Never share a cause between two chains; a leaked node shrinks the pool for good.
- **Context goes into the error, not the log:** the payload of the tag carries the ID, pin, value, limits. Drivers don't log.
- **Steps that must all run** (teardown, sweeps, per-channel work) return the first error and free the rest (`SYS_DEVICE_KEEP_FIRST`); report a later one first where it matters.
- **Device errors** are wrapped by the registry (`ERR_DEV_ID{id}`, `ERR_DEV_OP{op}`), so a driver returns plain causes. A driver's own task or listener has no caller and wraps its own error with its ID before reporting it. A device that fails a range check uses `ERR_DEV_RANGE` (SYS_DEVICE.MD).
- **A routed command's errors** are answered and reported with the command (`router` does it); a handler only returns them.
- **Severity follows the consequence.** Each tag has a default in its map; a call site that knows the real cost sets it with the `_SEV` variant. CRITICAL is for faults that must stop the board (the policy stops the VM and suspends every device). A context-only wrapper tag is NONE.
- **New tags** go at the end of the module's `ERR_<MODULE>_MAP` with the next code; a new module takes an ID in `err_modules.h`. Never renumber. Then `sh templates/sync.sh` (regenerates `errors.json`).
- **Control flow never depends on an error being allocated:** when the pool is empty `ERR_NEW` returns the static `ERR_POOL_EMPTY` and `ERR_WRAP` returns the cause unchanged.
