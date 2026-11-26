# errors — architecture (for agents)

Component `components/core/errors/`. The foundational error handling and propagation framework for the firmware. Defines the chain model, fixed block pool, severity calculation, tag registration, logging, and wire encoding. Include `errors.h` only.

## Files

| File | Role |
|---|---|
| `errors.h` / `errors.c` | The public API: types (`err_h`: an opaque node, `err_sev_e`), creating and reading a node (`err__new`, `err_tag` / `err_cause` / `err_data` / `err_find`), the severity and root of a chain (`err_sev`, `err_chain_sev`, `err_root_tag`, `err_drop`), `err_free`, `err_report` and `err_boot` (in `err_sink.c`: the end of a constructor's registration, which aborts); includes the core tags (`errors/core_errors.h`), which `errors.c` registers |
| `err_macros.h` | Every macro: the tags (`ERR_TAG`, `ERR_TAGS_DECLARE`), the node size (`ERR_HDR_LEN` / `ERR_DATA_MAX`), the registration of a module's map (`ERR_MODULE_DEFINE`), constructors (`ERR_NEW` / `ERR_WRAP` / `ERR_DATA`) and propagation (`ERR_TRY`, `ERR_CHECK*`, `ERR_KEEP_FIRST`, `ERR_ESP`); and what only they use, never called directly: `err_info_t` (where a node was made, built at the call site), `err_tag_info_t`, `err_module_t`, `err__register_module`, `err__new`. Included by `errors.h` |
| `err_sink.h` / `err_sink.c` | The sink (`err_set_sink`, set by the app at boot) and the reports: `err_report`, `err_report_ex` (a command's chain, `struct cmd_req`) |
| `err_log.h` / `err_log.c` | `err_log` (a chain as log text, for a sink that prints a chain it must not report), `err_log_ctx`, `err_log_enable`: chain formatting to `ESP_LOG` |
| `err_record.h` / `err_record.c` | The error record: `err_record_src_e`, the flags, `err_record_cmd_t`, `err_record` / `err_record_cmd` (a chain with what caused it, as bytes) and `err_record_parse` (a record made a chain again); for the modules that send, keep or read records |
| `err_tags.c` | Module and tag registry (`err__register_module`), lookup (`err_tag_info`, `err_module`) |
| `err_pool.c` | Fixed block pool (one bit per block, taken and returned with atomics, no lock; task- and ISR-safe, double-free detection, static storage) |
| `internal/err_internal.h` | Shared by the component's own files only (`PRIV_INCLUDE_DIRS`; a host test that must reach a node includes it): the node (`struct err`), `ERR_MODULE` / `ERR_CODE` (a tag's parts), the pool, the tag lookup (`err_tag_info`, `err_module`); a static check that `ERR_HDR_LEN` is the node's header |
| `errors.json` | Generated error catalog descriptor for all firmware modules: tags, payload fields, what an id field refers to, the user message of each tag |
| `errors/core_errors.h` / `errors/errors_core.json` | The core tags, as every module's errors: `MODULE_ID_CORE` (no module), `ERR_CORE_MAP` (`ERR_POOL_EMPTY`, `ERR_ESP_ERR`, `ERR_RECORD_CUT`, `ERR_NULL_PTR`, `ERR_RANGE`, `ERR_RANGE_F`) and their user messages (and hints: what to do). Each module has its map `<component>_errors.h` and its `errors_<module>.json` in the `errors/` folder of its component |
| `errors_id.h` | `MODULE_ID_ERRORS`: the number of the errors' stream (its port in the data matrix, its PUSH key; DATA_MATRIX.MD "Every byte of a provider through the matrix"); not the tags' module, which is each module's own |
| `CMakeLists.txt` | Component registration and source files |
| `Kconfig` | Configuration for call-site info, pool size, line buffer length, module limits |
| `host/` | Host tests: `test_errors.c`, `host_test.json`, `errors_host_config.h` (see Tests); not part of the firmware build |
| `AGENTS.md` | Rules for changing this folder |

## Model

- An error is a **chain** of nodes. `err_h` = pointer to the outermost node; **`NULL` = success**. The node is opaque: its layout is in `err_internal.h`, the rest of the firmware reads it with `err_tag(e)`, `err_cause(e)` (the deeper node, `NULL` at the root cause), `err_data(e)` / `ERR_DATA` and `err_find(chain, tag)` (the first node with that tag, from the top, or `NULL`). Each answers 0 / `NULL` for `NULL`.
- One node = one pool block: `[info*] next tag data[]` (header 12 B with call-site info, 8 B without on the board's 4-byte pointers; payload max `ERR_DATA_MAX` = block − header, 20 B by default). Packed, no padding.
- `tag = ERR_TAG(module, code)`: high 16 bits module, low 16 bits code; compared as a whole. Tag 0 = none / success.
- `info` = pointer to a `static const err_info_t {file, func, line}` per call site (flash, not RAM); `NULL` for the static `ERR_POOL_EMPTY` node, which has no call site (`err_log` then prints no location).
- Payload = the tag's packed struct `err_data_<TAG>_t`, read with `ERR_DATA(e, TAG)` after checking `err_tag(e)` (or on the node `err_find` gave).

## Tags and modules

A module declares tags once as an X-macro map in its header, and registers them in one `.c`:

```c
MODULE_ID_DECLARE(MODULE_ID_I2C, 0x..)                 // sys_i2c_id.h: the module's number (module_id.h)
#define ERR_I2C_MAP(X, M) \
  X(M, ERR_I2C_NACK, 1, ERR_SEV_HIGH, { uint8_t addr; uint8_t reg; })
ERR_TAGS_DECLARE(MODULE_ID_I2C, ERR_I2C_MAP)           // header: enum (ERR_TAG(module, code)) + err_data_*_t
ERR_MODULE_DEFINE(MODULE_ID_I2C, ERR_I2C_MAP)          // one .c: table + constructor registration
```

A row is `X(M, NAME, code, severity, { payload })`: `M` is the module the macros pass in, the code 1..0xFFFF within it. The module's name in the log and in `errors.json` is its `MODULE_ID_` name without the prefix (`I2C`).

- `errors` never includes module headers; it only looks tags up in registered tables (`err_tag_info`, `err_module`, internal).
- Compile-time checks: a code in 1..0xFFFF, payload fits a node, no duplicate codes. Registration asserts: module ID is unique, and count fits `ERRORS_MAX_MODULES`.
- A module's errors are under its number, `MODULE_ID_<NAME>` (`MODULE_ID_DECLARE` in the module's `<module>_id.h`, `module_id.h`: the same number as its router key and the module of its events); `MODULE_ID_CORE` is `0` (`errors.h`).
- A context-only wrapper tag (adds data, not a failure of its own) gets severity NONE, so it never raises the chain (e.g. `ERR_I2C_REG`).
- Put `ERR_MODULE_DEFINE` in a `.c` that the firmware references anyway — an unreferenced object is dropped by the linker together with its registration — or in the module's own `errors/<component>_errors.c` with the component linked `WHOLE_ARCHIVE` (`cmd`, `data_matrix`).

## Creating and propagating

| Macro | Description |
|---|---|
| `ERR_NEW(TAG, .f = v)` / `ERR_WRAP(cause, TAG, …)` | New root error / new node over `cause` (takes ownership) |
| `ERR_TRY(call)` | Return the error of `call` if any |
| `ERR_CHECK(cond, TAG, …)` | Return a new error if `cond` is false |
| `ERR_CHECK_RANGE(v, lo, hi)` | Return `ERR_RANGE {value, min, max}` unless `lo <= v <= hi`; `ERR_RANGE_F` for a `float` or `double` `v` (`_Generic` on the type of `v`, evaluated once). The check is exact, the payload is `int32_t` / `float` |
| `ERR_CHECK_IN_RANGE(v, lo, hi, TAG, …)` | The same check with the module's own tag and payload |
| `err_in_range(v, lo, hi)` | `lo <= v <= hi`, compared in `int64_t`, in `double` for a `float` / `double` `v` (NaN never in range; no `-Wtype-limits` warning for an unsigned `v` and `lo` 0) |
| `ERR_KEEP_FIRST(acc, call)` | Run `call`, keep the first error in `acc`, free a later one (teardown: every step runs) |
| `ERR_ESP(esp_call)` / `ERR_TRY_ESP(esp_call)` | `esp_err_t` → `NULL` or `ERR_ESP_ERR{code}` |

A payload that does not fit a node fails to compile in `ERR_MODULE_DEFINE`.

Rules:
- Return `err_h` from anything that can fail; **never return `NULL` for a failure**.
- `ERR_NEW` / `ERR_WRAP` (`err__new`) are `MUST_USE`. Whoever ends a chain frees it: `err_free(chain)` (whole chain). `err_log` borrows.
- Never share a cause between two chains. A leaked node permanently reduces the pool.
- **Pool empty**: `ERR_NEW` → shared static `ERR_POOL_EMPTY` node (never freed, no `info`); `ERR_WRAP` → returns `cause` unchanged (this level is lost).

## Severity

- `err_sev_e`: NONE < LOW < MEDIUM < HIGH < CRITICAL. **A tag has one severity**, in its map; a node carries none of its own.
- `err_sev(e)`: the node's tag's severity (NONE for an unknown tag).
- The error record carries no severity: the app reads each tag's from `errors.json`.
- `err_chain_sev(chain)`: the highest severity of the chain's nodes. Wrapping never lowers a cause: an expected failure is handled (freed) where it is expected, not reported.
- `err_root_tag(chain)`: the tag of the deepest node (0 for NULL).
- `err_drop(&err, tag)`: an expected cause taken away. When the root cause of `err` is `tag` the chain is freed, `err` becomes NULL and it answers true; any other error stays. For an error that is an answer, not a failure: an op the device doesn't have (`sys_io_prepare`), a probe that found nothing (an I2C bus's PROBE and SCAN), a device not created yet. `if (err_drop(&err, ERR_I2C_DEV_NOT_FOUND)) { continue; } ERR_TRY(err);`
- Severity picks the log level (HIGH/CRITICAL → error, MEDIUM → warn, else info). The response layer is the device error policy (SYS_DEVICE.MD "Error policy"), built on `err_chain_sev` and run by the app's sink.

## Logging

`err_log_ctx(chain, context)` prints `context` first (e.g. the failing command), at the chain's level; `err_log(chain)` is the same without it. `err_log(chain)`: one line per node, outermost first: `[i] NAME (module.code) SEV data: xx… @ file:line func()`. `ERR_ESP_ERR` prints `esp_err_to_name`. One log level for the whole chain (`err_chain_sev`). Both only borrow the chain: they never free it. `err_log_enable(false)` stops the text of every chain (`err_log`, `err_log_ctx`, so `err_report` too) without touching the sink: `sys_log` sets it from its `trace_errors` option. Task context only. Walks are bounded by `ERR_CHAIN_MAX` (cyclic chain guard).

## Reporting (end of a chain)

- `err_report(chain)` / `err_report_ex(chain, log_ctx, cmd)`: the end of a chain nobody returns any more (task loops, callbacks, boot). Logs it (context line first), passes it to the **sink** with `cmd` (the command it belongs to, NULL for an internal error), frees it. The command is `cmd_req_t` (`cmd_types.h` in `utils`, below `errors`): the sink gets it as it is, the record takes its origin and request.
- `err_set_sink(fn)` (boot, by the app): `fn(chain, cmd)` turns every reported chain into an error record (below) out of the port `ERRORS` (`MODULE_ID_ERRORS`, SYS_LOG.MD) and in the history (SYS_LOG.MD).
- The sink must not report errors itself (loop); code on the reporting path (stream task, log capture) uses `err_log` / `err_free` instead.
- Rule: return `err_h` while there is a caller; `err_report` where the chain ends. Plain `err_log` + `err_free` only where reporting could loop.

## Error record

`err_record_cmd(cmd, chain, buf, cap)` (`err_record.h`): a chain with the command it belongs to (`cmd_req_t`, `cmd_types.h` in `utils`; NULL gives the internal record). An internal record is also `err_record(chain, buf, cap)`. The same bytes are the router's ERROR answer (ROUTER.MD), every record on the errors stream (the sink in `runit`, SYS_LOG.MD) and the kept history (SYS_LOG.MD ERRORS).

`cmd_req_t` (`cmd_types.h`) is `{req, req_len, seq u16, src u8, flags u8}`: the request and who sent it (`src`, `CMD_SRC_*` in cmd.h: the board or client id of the frame's header, or `CMD_SRC_SELF` for the board itself). No board id: the frame that carries a record names its board.

```
[src u8]                                    0 internal (no request), 1 command
[unix_ms u64 LE]                            the board's clock when it was reported (`board_time_now_ms`, TIME.MD; ERR_RECORD_STAMP_BYTES); before 2020: not a date, a time since the start
src 1: err_record_cmd_t (5 B, after the stamp)
       [from u8]     cmd_req_t.src: who sent the command
       [seq u16 LE]  cmd_req_t.seq: the sender's counter
       [flags u8]    ROUTER_FLAG_*
       [req_len u8]  bits 0-6 = request bytes that follow, bit 7 = request cut (ERR_RECORD_REQ_CUT)
       [request]     key, dir, keys + payload as received (no frame header)
[chain]                                     [count u8, bit 7 = cut] then per node [tag u32 LE][len u8][payload len B]
```

- Chain: `payload` is the packed `err_data_<TAG>_t` as is (`len` from the tag table, 0 for an unknown tag). A node's severity is not sent: it is its tag's, which the app reads from `errors.json` with the payload's fields.
- The request is cut to `CONFIG_ERRORS_RECORD_REQ_MAX` (32) bytes, and further when `cap` is small; the chain gets the rest (its own truncated bit). Every caller gives `ERR_RECORD_MAX` as `cap`.
- The chain starts with the route (`ERR_ROUTER_KEY{key}` at the root, `ERR_ROUTER_OP{code}` in a module; NONE severity) and ends with the root cause, so it shows how far routing got; the request shows what was asked.
- Command errors end in `err_report_ex(err, ctx_line, &cmd)` (the router, for a command that failed): the log gets a context line before the chain (`cmd src f0 seq 7 flags 00 req 02 00 02 00`) and the sink gets the command, so the errors stream carries the same record as the answer, and a sender finds its own errors there by `from` and `seq`.
- `src 0` (internal: device task, timer, boot) has no command header.
- `err_record_parse(data, len, &chain)` makes a record a chain again, its nodes outermost first; the source and the command header are skipped, a tag this board doesn't know keeps its payload as is (cut to `ERR_DATA_MAX`). With the pool short the outermost nodes are left out. A record cut short: `ERR_RECORD_CUT{len}`; no place for the chain, or no bytes where `len` promises some: `ERR_NULL_PTR`.

Example (an ERROR answer), a read of INFO of device 9 that is not there, from the client `f0` (SYS_SESSION.MD "Frames"), as the board sent it (a field in brackets is its bytes):

```
request   [REQUEST][seq 1] | [MODULE_ID_SYS_DEVICE][0 read][9][INFO u16]
answer    [ANSWER, status ERROR][seq 1]
          [1 command][stamp u64]                       clock not set: the time since the start
          [f0][seq 1][flags 0]                         from f0, seq 1
          [5][MODULE_ID_SYS_DEVICE][0][9][INFO u16]    request: 5 B
          [4]                                          chain: 4 nodes
          [ERR_ROUTER_KEY u32][1][MODULE_ID_SYS_DEVICE]  key: the device router
          [ERR_DEV_ID u32][1][9]                       id 9
          [ERR_DEV_OP u32][3][INFO u16][0]             op INFO, dir read
          [ERR_DEV_NOT_FOUND u32][1][9]                id 9
```

## Core Errors (`MODULE_ID_CORE`)

| Error | Severity | Context / Fields | Description |
|---|---|---|---|
| `ERR_POOL_EMPTY` | LOW | – | Error node pool exhausted; static fallback node returned |
| `ERR_ESP_ERR` | HIGH | `esp_err_t code` | Underlying ESP-IDF driver or system call failed |
| `ERR_RECORD_CUT` | MEDIUM | `u16 len` | `err_record_parse`: a record cut short |
| `ERR_NULL_PTR` | HIGH | – | A C function got a NULL pointer: a bug of its caller, not a bad value in a command (that is the module's own tag, `ERR_CMD_BAD_ARG` for a frame) |
| `ERR_RANGE` | HIGH | `i32 value, i32 min, i32 max` | `ERR_CHECK_RANGE`: a value outside `min..max` |
| `ERR_RANGE_F` | HIGH | `f32 value, f32 min, f32 max` | `ERR_CHECK_RANGE` of a `float` / `double` value |

## Limits

- `CONFIG_ERRORS_POOL_BLOCKS` (default 32): maximum concurrent live error nodes in the pool. `ERR_CHAIN_MAX` (`err_macros.h`) = this + 1: the most nodes any walk along a chain visits (the errors component's and a module's that walks with `err_cause`), so a corrupted, cyclic chain never loops forever.
- `CONFIG_ERRORS_POOL_BLOCK_SIZE` (default 32 B): block size. Node payload is limited to `ERR_DATA_MAX` (20 B with call-site info, 24 B without on the board).
- `CONFIG_ERRORS_MAX_MODULES` (default 32): maximum number of modules that can register error tables.
- `CONFIG_ERRORS_LOG_LINE_LEN` (default 256 B): stack buffer size for formatting chain lines in `err_log`.
- `CONFIG_ERRORS_RECORD_REQ_MAX` (default 32 B): request bytes an error record repeats (7 bits at most).
- `CONFIG_ERRORS_RECORD_NODES` (default 6): chain nodes at their largest payload a record has room for. With the request it sizes `ERR_RECORD_MAX` (`err_record.h`, 197 B by default): the one bound of every record, so the router's ERROR answer, the errors stream, the history, the policy's queue and a remote device's error carry the same bytes.

## Tests

Host tests in `host/`, run by the shared runner (`runit-esp/test_host/HOST_TESTS.MD`): `python test_host/run.py errors` from `runit-esp/`. The component's real sources are built with `gcc` on the PC with Unity; only the platform is replaced (clock, log, sdkconfig). `host/host_test.json` lists the sources and three variants: `info` (call-site info on), `no_info` (`CONFIG_ERRORS_INFO=0`) and `no_assert` (`NDEBUG`: what a misuse does where asserts are off); `host/errors_host_config.h` holds the Kconfig defaults. The host has 8-byte pointers, so the node header is `2 * sizeof(void*) + 4` (`ERR_HDR_LEN`): the tests check the logic and the wire bytes, not the board's node size. No test waits: the concurrency test joins its threads, nothing sleeps.

They cover:
- **Nodes:** tag and payload, a payload of exactly `ERR_DATA_MAX`, none (`data` NULL), the rest of a reused block zeroed; reading a node (`err_tag`, `err_cause`, `err_data`, NULL).
- **Chains:** order, root tag (also of one node), severity of a node and a chain (NULL, an unknown tag), `err_find` (the outermost of two, none, NULL), `err_drop` (only the root cause it names, a one-node chain, the whole chain freed, NULL).
- **The pool:** blocks distinct, aligned and reusable; exhaustion with the static `ERR_POOL_EMPTY` (always the same node) and `ERR_WRAP` keeping the cause; the static node as a cause, never freed; four threads allocating and freeing at once without a block handed out twice or lost; every test returning every block. With asserts off: a double free, a pointer outside the pool and one inside a block ignored, the pool still handing out every block once; a payload longer than a node cut without touching another block.
- **The registry:** lookup, an unknown tag or module.
- **The macros:** `ERR_TRY` / `ERR_CHECK` / `ERR_CHECK_IN_RANGE` / `ERR_CHECK_RANGE` (the tag by the type of `v`, the payload, `v` evaluated once, NaN) / `ERR_KEEP_FIRST` (a success first, a later error freed) / `ERR_ESP` / `ERR_TRY_ESP`.
- **The record:** its bytes (internal, command with a request cut by `ERRORS_RECORD_REQ_MAX` and by the capacity, board, cut by capacity); its parse (round trip, command and board headers skipped, every cut-short prefix, NULL arguments, a tag this board does not know, a short pool keeping the root cause, random bytes without crash or leak).
- **Guards:** a cyclic chain stays bounded in every walk.
- **The log:** lines, an ESP-IDF code by name, the call site (and none without call-site info), level by severity, a context line, nothing for NULL, off.
- **Reporting:** `err_report` / `err_report_ex` (sink, context line, the command passed on, freeing, NULL).

Not covered on the host: the pool's use from an ISR (threads cover its atomics between tasks, an ISR is not run); the assertions themselves (they abort: the `no_assert` variant covers what follows them); the registration's asserts (a module ID twice, more modules than `ERRORS_MAX_MODULES`).
