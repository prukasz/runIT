# errors — architecture (for agents)

Component `components/core/errors/`. The foundational error handling and propagation framework for the firmware. Defines the chain model, fixed block pool, severity calculation, tag registration, logging, and wire encoding. Include `errors.h` only.

## Files

| File | Role |
|---|---|
| `errors.h` | Types (`err_t`/`err_h`, `err_sev_e`, `err_tag_info_t`, `err_module_t`), core API, core tags (module 0) |
| `err_modules.h` | Module IDs (`ERR_MOD_*`) — the single source of truth for module assignment |
| `err_macros.h` | All macros: `ERR_TAG`, `ERR_HDR_LEN`/`ERR_DATA_MAX`, X-macro constructors, `ERR_TRY*` propagation. Included by `errors.h` |
| `errors.c` | `err_new`, `err_free`, `err_sev`, `err_chain_sev`, core module registration |
| `err_pool.[ch]` | Fixed block pool (task- and ISR-safe, double-free detection, static storage) |
| `err_tags.c` | Module and tag registry lookup (`err_tag_info`, `err_module`) |
| `err_log.c` | `err_log`, `err_log_ctx`, `err_log_enable` — chain formatting to `ESP_LOG` |
| `err_wire.c` | `err_encode` — binary serialization for router error answers and streams |
| `errors.json` | Generated error catalog descriptor for all firmware modules: tags, payload fields, what an id field refers to, the user message of each tag |
| `errors_core.json` | The user messages of this module's tags (and their hints: what to do) (each module has its `errors_<module>.json` next to its map) |
| `CMakeLists.txt` | Component registration and source files |
| `Kconfig` | Configuration for call-site info, pool size, line buffer length, module limits |
| `AGENTS.md` | Rules for changing this folder |

## Model

- An error is a **chain** of nodes. `err_h` = pointer to the outermost node; **`NULL` = success**. `next` = the cause (deeper node), `NULL` at the root cause.
- One node = one pool block: `[info*] next tag sev data[]` (header 13 B with call-site info, 9 B without; payload max `ERR_DATA_MAX` = block − header, 19 B by default). Packed, no padding.
- `tag = ERR_TAG(module, code)`: high 16 bits module, low 16 bits code; compared as a whole. Tag 0 = none / success.
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
- Compile-time checks: payload fits a node, no duplicate tag IDs. Registration asserts: tag belongs to the module, module ID is unique, and count fits `ERRORS_MAX_MODULES`.
- Module IDs (`ERR_MOD_*`) must be unique firmware-wide: add new ones to `err_modules.h`. Current modules:
  - `ERR_MOD_CORE` = 0 (`ERR_POOL_EMPTY`, `ERR_ESP_ERR`)
  - `ERR_MOD_SYS_I2C` = 1
  - `ERR_MOD_ROUTER` = 2
  - `ERR_MOD_SYS_UART` = 3
  - `ERR_MOD_SYS_BLE` = 4
  - `ERR_MOD_SYS_CONNECTOR` = 5
  - `ERR_MOD_SYS_DEVICE` = 6
  - `ERR_MOD_SYS_IO` = 7
  - `ERR_MOD_SYS_ACTIONS` = 8
  - `ERR_MOD_SYS_EVENT` = 9
  - `ERR_MOD_SYS_POWER` = 10
  - `ERR_MOD_CMD` = 11
  - `ERR_MOD_SYS_LOG` = 13
  - `ERR_MOD_SYS_POWER_MANAGER` = 14
  - `ERR_MOD_VM` = 15
  - `ERR_MOD_SYS_PROJECT` = 16
- A context-only wrapper tag (adds data, not a failure of its own) gets default severity NONE, so it never raises the chain (e.g. `ERR_I2C_REG`).
- Put `ERR_MODULE_DEFINE` in a `.c` that the firmware references anyway — an unreferenced object is dropped by the linker together with its registration.

## Creating and propagating

| Macro | Description |
|---|---|
| `ERR_NEW(TAG, .f = v)` / `ERR_WRAP(cause, TAG, …)` | New root error / new node over `cause` (takes ownership) |
| `ERR_TRY(call)` | Return the error of `call` if any |
| `ERR_TRY_WRAP(call, TAG, …)` | Same, wrapped with this level's node |
| `ERR_CHECK(cond, TAG, …)` | Return a new error if `cond` is false |
| `ERR_ESP(esp_call)` / `ERR_TRY_ESP(esp_call)` | `esp_err_t` → `NULL` or `ERR_ESP_ERR{code}` |
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
- Severity picks the log level (HIGH/CRITICAL → error, MEDIUM → warn, else info). The response layer is the device error policy (SYS_DEVICE.MD "Error policy"), built on `err_chain_sev` and run by the app's sink.

## Logging

`err_log_ctx(chain, context)` prints `context` first (e.g. the failing command), at the chain's level; `err_log(chain)` is the same without it. `err_log(chain)`: one line per node, outermost first: `[i] NAME (module.code) SEV [(default X)] data: xx… @ file:line func()`. `ERR_ESP_ERR` prints `esp_err_to_name`. The `(default X)` suffix marks an overridden severity. One log level for the whole chain (`err_chain_sev`). `err_log_enable(false)` stops the text of every chain (`err_log`, `err_log_ctx`, so `err_report` too) without touching the sink: `sys_log` sets it from its `trace_errors` option. Task context only. Walks are bounded by `ERRORS_POOL_BLOCKS + 1` (cyclic chain guard).

## Reporting (end of a chain)

- `err_report(chain)` / `err_report_ex(chain, log_ctx, origin)`: the end of a chain nobody returns any more (task loops, callbacks, boot). Logs it (context line first), passes it to the **sink** with `origin`, frees it.
- `err_set_sink(fn)` (boot, by the app): turns every reported chain into a router error record on the errors connector (SYS_CONNECTOR.MD). `origin` = the router's `router_cmd_t` for command errors, NULL otherwise.
- The sink must not report errors itself (loop); code on the reporting path (connector task, log capture) uses `err_log` / `err_free` instead.
- Rule: return `err_h` while there is a caller; `err_report` where the chain ends. Plain `err_log` + `err_free` only where reporting could loop.

## Wire format

`err_encode(chain, buf, cap)` → bytes written. Outermost node first:

```
[count u8, bit 7 = truncated]  then per node  [tag u32 LE][sev u8][len u8][payload len B]
```

`sev` is the node's effective severity (`err_sev`), `payload` the packed `err_data_<TAG>_t` as is (`len` from the tag table, 0 for an unknown tag). Nodes that don't fit are left out and bit 7 is set. Used by router error responses (ROUTER.MD); the app decodes payloads with the tag tables.

## Core Errors (`ERR_MOD_CORE` = 0)

| Error | Severity | Context / Fields | Description |
|---|---|---|---|
| `ERR_POOL_EMPTY` | LOW | – | Error node pool exhausted; static fallback node returned |
| `ERR_ESP_ERR` | HIGH | `esp_err_t code` | Underlying ESP-IDF driver or system call failed |

## Limits

- `CONFIG_ERRORS_POOL_BLOCKS` (default 32): maximum concurrent live error nodes in the pool.
- `CONFIG_ERRORS_POOL_BLOCK_SIZE` (default 32 B): block size. Node payload is limited to `ERR_DATA_MAX` (19 B with call-site info, 23 B without).
- `CONFIG_ERRORS_MAX_MODULES` (default 32): maximum number of modules that can register error tables.
- `CONFIG_ERRORS_LOG_LINE_LEN` (default 256 B): stack buffer size for formatting chain lines in `err_log`.

## Tests

Core error unit tests cover:
- Pool allocation and release, spinlock concurrency, and double-free detection assertion.
- Pool exhaustion behavior (`ERR_POOL_EMPTY` static fallback, `ERR_WRAP` preservation of cause).
- Chain severity walk resolution (`err_chain_sev`) with tag defaults and explicit overrides.
- Wire serialization (`err_encode`) including truncation flag when capacity is exceeded.
- Translation of `esp_err_t` return codes via `ERR_ESP` / `ERR_TRY_ESP`.
