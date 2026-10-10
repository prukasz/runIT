# errors — architecture (for agents)

Component `components/core/errors/`. Replaces the legacy `sys_errors` (`legacy/`); used by `sys_i2c`, `router`, `sys_uart`. Include `errors.h` only.

## Files

| File | Role |
|---|---|
| `errors.h` | Types (`err_t`/`err_h`, `err_sev_e`, `err_tag_info_t`, `err_module_t`), function declarations, core tags (module 0) |
| `err_modules.h` | Module IDs (`ERR_MOD_*`) — the one place they are assigned. Included by `errors.h` |
| `err_macros.h` | **All macros**: `ERR_TAG`, `ERR_HDR_LEN`/`ERR_DATA_MAX`, tag registry X-macros, constructors (`ERR_NEW`/`ERR_WRAP`…), propagation (`ERR_TRY`…). Included by `errors.h` — never include it directly |
| `errors.c` | `err_new`, `err_free`, `err_sev`, `err_chain_sev`; core module registration |
| `err_pool.[ch]` | Fixed block pool (task + ISR safe, double-free detection, min-free watermark) |
| `err_tags.c` | Module/tag registry and lookup |
| `err_log.c` | `err_log` — chain to `ESP_LOG` |
| `err_wire.c` | `err_encode` — chain to bytes (error responses) |
| `Kconfig` | `ERRORS_INFO_FILE/FUNC/LINE`, `ERRORS_POOL_BLOCKS` (32), `ERRORS_POOL_BLOCK_SIZE` (32), `ERRORS_LOG_LINE_LEN` (256), `ERRORS_MAX_MODULES` (32) |

## Model

- An error is a **chain** of nodes. `err_h` = pointer to the outermost node; **`NULL` = success**. `next` = the cause (deeper), `NULL` at the root cause.
- One node = one pool block: `[info*] next tag sev data[]` (header 13 B, 9 B without call-site info; payload max `ERR_DATA_MAX` = block − header, 19 B by default). Packed, no padding.
- `tag = ERR_TAG(module, code)`: high 16 bits module, low 16 bits code; compared as a whole. Tag 0 = none.
- `info` = pointer to a `static const err_info_t {file, func, line}` per call site (flash, not RAM).
- Payload = the tag's packed struct `err_data_<TAG>_t`, read with `ERR_DATA(e, TAG)` after checking `e->tag`.

## Tags and modules

A module declares tags once as an X-macro map in its header, and registers them in one `.c`:

```c
#define ERR_I2C_MAP(X) \
  X(ERR_I2C_NACK, ERR_TAG(ERR_MOD_I2C, 1), ERR_SEV_HIGH, { uint8_t addr; uint8_t reg; })
ERR_TAGS_DECLARE(ERR_I2C_MAP)                          // header: enum + err_data_*_t
ERR_MODULE_DEFINE(ERR_MOD_I2C, "i2c", ERR_I2C_MAP)     // one .c: table + constructor registration
```

- `errors` never includes module headers; it only looks tags up in registered tables (`err_tag_info`, `err_module`).
- Compile-time checks: payload fits a node, no duplicate tag IDs. Registration asserts: tag belongs to the module, module ID unique, `ERRORS_MAX_MODULES`.
- Module IDs (`ERR_MOD_*`) must be unique firmware-wide: add new ones to `err_modules.h`. `ERR_MOD_CORE` = 0 (`ERR_POOL_EMPTY`, `ERR_ESP_ERR`), `ERR_MOD_SYS_I2C` = 1, `ERR_MOD_ROUTER` = 2, `ERR_MOD_SYS_UART` = 3, `ERR_MOD_SYS_BLE` = 4, `ERR_MOD_SYS_CONNECTOR` = 5, `ERR_MOD_SYS_DEVICE` = 6 (also the errors every device raises: range, pin), `ERR_MOD_SYS_IO` = 7 (devices with pins: mode, PWM pools), `ERR_MOD_SYS_ACTIONS` = 8, `ERR_MOD_SYS_EVENT` = 9.
- A context-only wrapper tag (adds data, not a failure of its own) gets default severity NONE, so it never raises the chain (e.g. `ERR_I2C_REG`).
- Put `ERR_MODULE_DEFINE` in a `.c` that the firmware references anyway — an unreferenced object is dropped by the linker together with its registration.

## Creating and propagating

| Macro | Does |
|---|---|
| `ERR_NEW(TAG, .f = v)` / `ERR_WRAP(cause, TAG, …)` | New root error / new node over `cause` (takes ownership) |
| `ERR_TRY(call)` | Return the error of `call` if any |
| `ERR_TRY_WRAP(call, TAG, …)` | Same, wrapped with this level's node |
| `ERR_CHECK(cond, TAG, …)` | Return a new error if `cond` is false |
| `ERR_ESP(esp_call)` / `ERR_TRY_ESP(esp_call)` | `esp_err_t` → `NULL` or `ERR_ESP_ERR{code}` (hide ESP codes from upper layers) |
| `ERR_*_RAW` | Untyped payload `(data, len)` |

Every macro has a `_SEV` variant with the severity as an extra argument (`ERR_NEW_SEV`, `ERR_WRAP_SEV`, `ERR_TRY_WRAP_SEV`, `ERR_CHECK_SEV`, `ERR_ESP_SEV`, `ERR_TRY_ESP_SEV`, `ERR_*_RAW_SEV`).

Rules:
- Return `err_h` from anything that can fail; **never return `NULL` for a failure**.
- `err_new` is `MUST_USE`. Whoever ends a chain frees it: `err_free(chain)` (whole chain). `err_log` borrows.
- Never share a cause between two chains. A leaked node permanently reduces the pool.
- **Pool empty**: `ERR_NEW` → shared static `ERR_POOL_EMPTY` node (never freed); `ERR_WRAP` → returns `cause` unchanged (this level, and its severity, is lost). So severity is passed into the constructor and never set afterwards on the returned node.

## Severity

- `err_sev_e`: NONE < LOW < MEDIUM < HIGH < CRITICAL. Each **tag has a default** (in its map). A **node may override** it at creation via `_SEV` (a runtime value is fine): only the call site knows the cost (a failed malloc in the logger = LOW, in device create = CRITICAL). Stored in `node->sev`; `ERR_SEV_DEFAULT` (0xFF) = use the tag's.
- `err_sev(e)`: node's effective severity (override, else tag default, else NONE).
- `err_chain_sev(chain)`: walks from the **outermost** node; unset nodes contribute their tag default (max wins); the **first node with an explicit severity contributes it and stops the walk** — that level's verdict on everything beneath, so it can raise *or lower* its causes. Outer unset nodes still count.
  - `ERR_WRAP_SEV(e, T, ERR_SEV_LOW, …)` over a HIGH cause → LOW.
  - `ERR_WRAP(e, T, …)` with T default HIGH over a LOW node → HIGH.
- Severity only picks the log level (HIGH/CRITICAL → error, MEDIUM → warn, else info). **No policy/reaction layer exists yet** (device response, system hooks) — it is meant to be built on `err_chain_sev`.

## Logging

`err_log_ctx(chain, context)` prints `context` first (e.g. the failing command), at the chain's level; `err_log(chain)` is the same without it. `err_log(chain)`: one line per node, outermost first: `[i] NAME (module.code) SEV [(default X)] data: xx… @ file:line func()`. `ERR_ESP_ERR` prints `esp_err_to_name`. The `(default X)` suffix marks an overridden severity. One log level for the whole chain (`err_chain_sev`). Task context only. Walks are bounded by `ERRORS_POOL_BLOCKS + 1` (cyclic chain guard).

## Reporting (end of a chain)

- `err_report(chain)` / `err_report_ex(chain, log_ctx, origin)`: the end of a chain nobody returns any more (task loops, callbacks, boot). Logs it (context line first), passes it to the **sink** with `origin`, frees it.
- `err_set_sink(fn)` (boot, by the app): `main` turns every reported chain into a router error record on the errors connector (SYS_CONNECTOR.MD). `origin` = the router's `router_cmd_t` for command errors, NULL otherwise.
- The sink must not report errors itself (loop); code on the reporting path (connector task, log capture) uses `err_log` / `err_free` instead.
- Rule: return `err_h` while there is a caller; `err_report` where the chain ends. Plain `err_log` + `err_free` only where reporting could loop.

## Wire format

`err_encode(chain, buf, cap)` → bytes written. Outermost node first:

```
[count u8, bit 7 = truncated]  then per node  [tag u32 LE][sev u8][len u8][payload len B]
```

`sev` is the node's effective severity (`err_sev`), `payload` the packed `err_data_<TAG>_t` as is (`len` from the tag table, 0 for an unknown tag). Nodes that don't fit are left out and bit 7 is set. Used by router error responses (ROUTER.MD); the app decodes payloads with the tag tables.

## Not there yet

- No repeat suppression on the errors stream, no repeat suppression, no device or owner attribution, no policy/hooks — the legacy `sys_errors` had these ([SYS_ERRORS.MD](../sys_errors/SYS_ERRORS.MD)); port only what is needed.
- The `runit-esp` skill's `errors.md` still describes the legacy `SE_*` API.
- `ERR_NO_MEM` and other shared tags do not exist yet (core has only `ERR_POOL_EMPTY`, `ERR_ESP_ERR`).
