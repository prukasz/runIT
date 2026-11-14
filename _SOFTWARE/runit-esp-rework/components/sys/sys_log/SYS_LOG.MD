# sys_log — logs and error records

Component `runit-esp-rework/components/sys/sys_log/`. Controls what `ESP_LOG*` prints, routes cleaned log lines to `SYS_STREAM_LOGS`, stores the latest error records for history reads, and sends error records to `SYS_STREAM_ERRORS`.

## Files

| File | Role |
|---|---|
| `sys_log.h` | API: `sys_log_init`, `sys_log_set`, `sys_log_get`, `sys_log_error`, `sys_log_error_get` |
| `sys_log.c` | `esp_log_set_vprintf` hook, ANSI strip + CR/LF trim, error history ring buffer, `ERR_MODULE_DEFINE`, `MODULE_ID_DEFINE(LOG, 0x2A)` |
| `router/sys_log_router.h` / `.c` | Remote interface (`MODULE_ID_LOG` `0x2A`, `LOG_OPERATIONS`), self-registers via `ROUTER_MODULE_REGISTER(LOG, sys_log_router)` |
| `router/contract_log.json` | Router contract descriptor (`router_key`: 42) |
| `errors/` | `MODULE_ID_DECLARE(LOG, 0x2A)` (`ERR_MOD_SYS_LOG`), `ERR_LOG_BAD_LEVEL{level, max}`, `errors_log.json` |
| `Kconfig` | `SYS_LOG_LINE_MAX` (default 160) and `SYS_LOG_ERRORS_KEPT` (default 16) |

## Behaviour

- **Defaults at boot (`sys_log_init`)**: `level = CONFIG_LOG_DEFAULT_LEVEL`, `mirror_serial = 0`, `trace_errors = 1`, `send_errors = 1`, `send_logs = 1`. Setting `mirror_serial = 0` keeps text `ESP_LOG` output off the console UART once `sys_log_init()` runs (important when `sys_uart` runs COBS framing on the same UART). Session binary frames and `SYS_STREAM_ERRORS` records never go through `ESP_LOG`, so log level settings never silence them.
- **vprintf hook (`log_vprintf`)**: skips stream queuing if the scheduler is not running, in ISR context, or running inside the stream task (`sys_stream_is_task()`), avoiding infinite recursion if stream delivery logs an error. Queue-full errors from `sys_stream_send()` are freed silently (`err_free`) so reporting does not recurse.
- **Error sink integration (`main`)**: the error sink converts reported chains via `cmd_err_record(cmd, chain, rec, sizeof(rec))` (up to `SYS_LOG_RECORD_MAX` = 192 bytes) and passes them to `sys_log_error()`, which records every error in the ring buffer regardless of `send_errors`, and forwards to `SYS_STREAM_ERRORS` when `send_errors != 0`.
- **Thread safety**: `s_log_preset.lock` (`portMUX_TYPE`) guards reads/writes of `s_log_preset.cfg` and the circular history buffer `s_log.history[]`.

## Remote (`sys_log_router`, root key `MODULE_ID_LOG` = 0x2A)

Requests are `[0x2A][dir][op][args]` (`0` read, `1` write). Op `0x02` is permanently unused.

| Op | Side | Request | Response |
|---|---|---|---|
| `0x01` `LOG_OP_CONFIG` | RW | `–` (R) / `sys_log_cfg_t` 5 B (W) | `sys_log_cfg_t` 5 B (R) / `–` (W) |
| `0x03` `LOG_OP_ERRORS` | R | `sys_log_errors_args_t` `{u8 index}` | `[u8 total][record]` (`[u8 total]` when `index >= total`) |
