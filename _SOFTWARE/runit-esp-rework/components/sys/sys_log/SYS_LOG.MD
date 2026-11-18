# sys_log — logs and error records

Component `runit-esp-rework/components/sys/sys_log/`. Controls what `ESP_LOG*` prints, routes cleaned log lines to `LOG_STREAM_LOGS`, stores the latest error records for history reads, and sends error records to `LOG_STREAM_ERRORS`.

## Files

| File | Role |
|---|---|
| `sys_log.h` | API: `sys_log_init`, `sys_log_set`, `sys_log_get`, `sys_log_report`, `sys_log_error`, `sys_log_error_get` |
| `sys_log.c` | `esp_log_set_vprintf` hook, ANSI strip + CR/LF trim, error history ring buffer, `ERR_MODULE_DEFINE` |
| `router/sys_log_router.h` / `.c` | Remote interface (`MODULE_ID_LOG` `0x2A`, `LOG_OPERATIONS`), self-registers via `ROUTER_MODULE_REGISTER(MODULE_ID_LOG, sys_log_router)` |
| `router/contract_log.json` | Router contract descriptor (`router_key`: 42) |
| `sys_log_id.h`, `errors/` | `MODULE_ID_DECLARE(MODULE_ID_LOG, 0x2A)` (`sys_log_id.h`), `ERR_LOG_BAD_LEVEL{level, max}`, `errors_log.json` |
| `Kconfig` | `SYS_LOG_LINE_MAX` (default 160) and `SYS_LOG_ERRORS_KEPT` (default 16) |

## Behaviour

- **Defaults at boot (`sys_log_init`)**: it tells the data matrix the module's streams (`LOG_STREAMS` in `sys_log.h`: `LOG_STREAM_LOGS` and `LOG_STREAM_ERRORS`, urgent), which go where its routes say (`app/runit` sets the board's own); `level = CONFIG_LOG_DEFAULT_LEVEL`, `mirror_serial = 0`, `trace_errors = 1`, `send_errors = 1`, `send_logs = 1`. Setting `mirror_serial = 0` keeps text `ESP_LOG` output off the console UART once `sys_log_init()` runs (important when `provider_console` runs COBS framing on the same UART). Session binary frames and `LOG_STREAM_ERRORS` records never go through `ESP_LOG`, so log level settings never silence them.
- **vprintf hook (`log_vprintf`)**: skips stream queuing if the scheduler is not running, in ISR context, or running inside the matrix's task (`data_matrix_is_task()`), avoiding infinite recursion if stream delivery logs an error. Queue-full errors from `data_matrix_send()` are freed silently (`err_free`) so reporting does not recurse.
- **Error sink integration (`app/runit`)**: the error sink calls `sys_log_report(chain, cmd)`, which builds the record with `cmd_err_record` (up to `SYS_LOG_RECORD_MAX` = 192 bytes) and passes it to `sys_log_error()`; that records every error in the ring buffer regardless of `send_errors`, and forwards to `LOG_STREAM_ERRORS` when `send_errors != 0`.
- **Thread safety**: `s_log_preset.lock` (`portMUX_TYPE`) guards reads/writes of `s_log_preset.cfg` and the circular history buffer `s_log.history[]`.

## Remote (`sys_log_router`, root key `MODULE_ID_LOG` = 0x2A)

Requests are `[0x2A][dir][op][args]` (`0` read, `1` write). Op `0x02` is permanently unused.

| Op | Side | Request | Response |
|---|---|---|---|
| `0x01` `LOG_OP_CONFIG` | RW | `–` (R) / `sys_log_cfg_t` 5 B (W) | `sys_log_cfg_t` 5 B (R) / `–` (W) |
| `0x03` `LOG_OP_ERRORS` | R | `sys_log_errors_args_t` `{u8 index}` | `[u8 total][record]` (`[u8 total]` when `index >= total`) |
