# errors — architecture (for agents)

Component `components/core/errors/`. The foundational error handling and propagation framework for the firmware. Defines the chain model, fixed block pool, severity calculation, tag registration, logging, and wire encoding. Include `errors.h` only.

## Files

| File | Role |
|---|---|
| `errors.h` | Types (`err_t`/`err_h`, `err_sev_e`, `err_tag_info_t`, `err_module_t`), core API, core tags (module 0) |
| `err_modules.h` | Module IDs (`ERR_MOD_*`) — the single source of truth for module assignment |
| `err_macros.h` | All macros: `ERR_TAG`, `ERR_HDR_LEN`/`ERR_DATA_MAX`, X-macro constructors, `ERR_TRY*` propagation. Included by `errors.h` |
| `errors.c` | `err_new`, `err_free`, `err_sev`, `err_chain_sev`, `err_root_tag`, core module registration |
| `err_internal.h` | Shared by the component's own files only: the pool, the tag lookup (`err_tag_info`, `err_module`), `err_log_ctx` |
| `err_pool.c` | Fixed block pool (task- and ISR-safe, double-free detection, static storage) |
| `err_tags.c` | Module and tag registry, lookup |
| `err_log.c` | `err_log`, `err_log_ctx`, `err_log_enable` — chain formatting to `ESP_LOG` |
| `err_wire.c` | `err_record`: the error record, a chain with its command, as bytes |
| `errors.json` | Generated error catalog descriptor for all firmware modules: tags, payload fields, what an id field refers to, the user message of each tag |
| `errors_core.json` | The user messages of this module's tags (and their hints: what to do) (each module has its `errors_<module>.json` in the `errors/` folder of its component, next to its map `<component>_errors.h`) |
| `CMakeLists.txt` | Component registration and source files |
| `Kconfig` | Configuration for call-site info, pool size, line buffer length, module limits |
| `AGENTS.md` | Rules for changing this folder |

## Model

- An error is a **chain** of nodes. `err_h` = pointer to the outermost node; **`NULL` = success**. `next` = the cause (deeper node), `NULL` at the root cause.
- One node = one pool block: `[info*] next tag data[]` (header 12 B with call-site info, 8 B without; payload max `ERR_DATA_MAX` = block − header, 19 B by default). Packed, no padding.
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

- `errors` never includes module headers; it only looks tags up in registered tables (`err_tag_info`, `err_module`, internal).
- Compile-time checks: payload fits a node, no duplicate tag IDs. Registration asserts: tag belongs to the module, module ID is unique, and count fits `ERRORS_MAX_MODULES`.
- Module IDs (`ERR_MOD_*`) are assigned only in `err_modules.h` (the list, in order; a new module takes the next number, none is renumbered or reused).
- A context-only wrapper tag (adds data, not a failure of its own) gets severity NONE, so it never raises the chain (e.g. `ERR_I2C_REG`).
- Put `ERR_MODULE_DEFINE` in a `.c` that the firmware references anyway — an unreferenced object is dropped by the linker together with its registration.

## Creating and propagating

| Macro | Description |
|---|---|
| `ERR_NEW(TAG, .f = v)` / `ERR_WRAP(cause, TAG, …)` | New root error / new node over `cause` (takes ownership) |
| `ERR_TRY(call)` | Return the error of `call` if any |
| `ERR_CHECK(cond, TAG, …)` | Return a new error if `cond` is false |
| `ERR_KEEP_FIRST(acc, call)` | Run `call`, keep the first error in `acc`, free a later one (teardown: every step runs) |
| `ERR_ESP(esp_call)` / `ERR_TRY_ESP(esp_call)` | `esp_err_t` → `NULL` or `ERR_ESP_ERR{code}` |

A payload that does not fit a node fails to compile in `ERR_MODULE_DEFINE`.

Rules:
- Return `err_h` from anything that can fail; **never return `NULL` for a failure**.
- `err_new` is `MUST_USE`. Whoever ends a chain frees it: `err_free(chain)` (whole chain). `err_log` borrows.
- Never share a cause between two chains. A leaked node permanently reduces the pool.
- **Pool empty**: `ERR_NEW` → shared static `ERR_POOL_EMPTY` node (never freed); `ERR_WRAP` → returns `cause` unchanged (this level is lost).

## Severity

- `err_sev_e`: NONE < LOW < MEDIUM < HIGH < CRITICAL. **A tag has one severity**, in its map; a node carries none of its own.
- `err_sev(e)`: the node's tag's severity (NONE for an unknown tag).
- `err_chain_sev(chain)`: the highest severity of the chain's nodes. Wrapping never lowers a cause: an expected failure is handled (freed) where it is expected, not reported.
- `err_root_tag(chain)`: the tag of the deepest node (0 for NULL), for a caller that tolerates one known cause (`sys_io_prepare` and `ERR_DEV_UNKNOWN_OP`).
- Severity picks the log level (HIGH/CRITICAL → error, MEDIUM → warn, else info). The response layer is the device error policy (SYS_DEVICE.MD "Error policy"), built on `err_chain_sev` and run by the app's sink.

## Logging

`err_log_ctx(chain, context)` prints `context` first (e.g. the failing command), at the chain's level; `err_log(chain)` is the same without it. `err_log(chain)`: one line per node, outermost first: `[i] NAME (module.code) SEV data: xx… @ file:line func()`. `ERR_ESP_ERR` prints `esp_err_to_name`. One log level for the whole chain (`err_chain_sev`). `err_log_enable(false)` stops the text of every chain (`err_log`, `err_log_ctx`, so `err_report` too) without touching the sink: `sys_log` sets it from its `trace_errors` option. Task context only. Walks are bounded by `ERRORS_POOL_BLOCKS + 1` (cyclic chain guard).

## Reporting (end of a chain)

- `err_report(chain)` / `err_report_ex(chain, log_ctx, cmd)`: the end of a chain nobody returns any more (task loops, callbacks, boot). Logs it (context line first), passes it to the **sink** with `cmd` (the `err_cmd_t` it belongs to, NULL for an internal error), frees it.
- `err_set_sink(fn)` (boot, by the app): `fn(chain, cmd)` turns every reported chain into an error record (below) on the errors connector (SYS_CONNECTOR.MD) and in the history (SYS_LOG.MD).
- The sink must not report errors itself (loop); code on the reporting path (connector task, log capture) uses `err_log` / `err_free` instead.
- Rule: return `err_h` while there is a caller; `err_report` where the chain ends. Plain `err_log` + `err_free` only where reporting could loop.

## Error record

`err_record(cmd, chain, buf, cap)` (`err_wire.c`): a chain with the command it belongs to (`err_cmd_t`, NULL for an internal error). The same bytes are the router's ERROR answer (ROUTER.MD), every record on the errors stream (the sink in `runit`, SYS_CONNECTOR.MD) and the kept history (SYS_LOG.MD ERRORS).

```
[src u8]                                    0 internal (no request), 1 command
[unix_ms u64 LE]                            the board's clock when it was reported (`board_time_now_ms`, TIME.MD; ERR_RECORD_STAMP_BYTES); before 2020: not a date, a time since the start
src 1: [link u8][seq u8][flags u8]               (after the stamp)
       [req_len u8]  bits 0-6 = request bytes that follow, bit 7 = request cut
       [request]     key, dir, keys + payload as received (no seq / flags)
[chain]                                     [count u8, bit 7 = cut] then per node [tag u32 LE][len u8][payload len B]
```

- Chain: `payload` is the packed `err_data_<TAG>_t` as is (`len` from the tag table, 0 for an unknown tag). A node's severity is not sent: it is its tag's, which the app reads from `errors.json` with the payload's fields.
- The request is cut to `CONFIG_ERRORS_RECORD_REQ_MAX` (32) bytes, and further when the link's frame is small; the chain gets the rest (its own truncated bit).
- The chain starts with the route (`ERR_ROUTER_KEY{key}` per level, NONE severity) and ends with the root cause, so it shows how far routing got; the request shows what was asked.
- Command errors end in `err_report_ex(err, ctx_line, &cmd)`: the log gets a context line before the chain (`cmd link 1 seq 7 flags 00 req 02 00 02 00`: the link ID, `transport_e`) and the sink gets the command, so the errors stream carries the same record as the response.
- `src 0` (internal: device task, timer, boot) has no command header.

Example (a router ERROR answer) — scan of bus 0 (only bus 1 is open):

```
request   07 00 | 02 00 02 00                        seq 7, normal, i2c read SCAN bus 0
response  05 07 01                                   stream, seq, ERROR
          01 | 00 00 00 00 00 00 00 00                   command; clock not set (time since the start, here 0)
          01 | 07 | 00                                UART, seq 7, flags 0
          04 | 02 00 02 00                            request: 4 B
          03                                          chain: 3 nodes
          01 00 02 00 01 02                           ERR_ROUTER_KEY        key 02 (i2c)
          01 00 02 00 01 02                           ERR_ROUTER_KEY        key 02 (SCAN)
          10 00 01 00 01 00                           ERR_I2C_REMOTE_DENIED bus 0
UART      #R:050701010000000000000000010700040200020003010002000102010002000102100001000100
```

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
- `CONFIG_ERRORS_RECORD_REQ_MAX` (default 32 B): request bytes an error record repeats (7 bits at most).

## Tests

Core error unit tests cover:
- Pool allocation and release, spinlock concurrency, and double-free detection assertion.
- Pool exhaustion behavior (`ERR_POOL_EMPTY` static fallback, `ERR_WRAP` preservation of cause).
- Chain severity walk resolution (`err_chain_sev`) with tag defaults and explicit overrides.
- The error record (`err_record`): the chain's cut bit when capacity is exceeded.
- Translation of `esp_err_t` return codes via `ERR_ESP` / `ERR_TRY_ESP`.
