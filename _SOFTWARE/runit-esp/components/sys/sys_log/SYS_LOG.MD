# sys_log — logs and error records

Component `runit-esp/components/sys/sys_log/`. Controls what `ESP_LOG*` prints, sends cleaned log lines out of its port `LOGS`, stores the latest error records for history reads, and sends error records out of its port `ERRORS`.

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

- **Ports and defaults at boot (`sys_log_init`)**: it registers the module's two ports of the data matrix (DATA_MATRIX.MD), each packet `[hdr PUSH, key][data]`: `LOGS` (`DATA_MATRIX_PORT(MODULE, MODULE_ID_LOG)`, out only: the log lines as text) and `ERRORS` (`DATA_MATRIX_PORT(MODULE, MODULE_ID_ERRORS)`, 0x31: this board's records out, urgent; other boards' records in, passed on to every port linked from it but the one they came from, `data_matrix_send_except`). Where they go is their links (`app/runit` makes the board's own). Then `level = CONFIG_LOG_DEFAULT_LEVEL`, `mirror_serial = 0`, `trace_errors = 1`, `send_errors = 1`, `send_logs = 1`. Setting `mirror_serial = 0` keeps text `ESP_LOG` output off the console UART once `sys_log_init()` runs (important when `provider_console` runs COBS framing on the same UART). Session binary frames and the records out of `ERRORS` never go through `ESP_LOG`, so log level settings never silence them.
- **vprintf hook (`log_vprintf`)**: sends nothing if the scheduler is not running, in ISR context, or while this task is already sending a line or a record (a per-task flag): a line logged inside a send, or an error reported inside one, would loop. Every send is `NOWAIT`: a full line drops it.
- **Error sink integration (`app/runit`)**: the error sink calls `sys_log_report(chain, cmd)`, which builds the record with `err_record_cmd` (up to `SYS_LOG_RECORD_MAX` = 192 bytes) and passes it to `sys_log_error()`; that records every error in the ring buffer regardless of `send_errors`, and sends it out of `ERRORS` when `send_errors != 0`.
- **Thread safety**: `s_log_preset.lock` (`portMUX_TYPE`) guards reads/writes of `s_log_preset.cfg` and the circular history buffer `s_log.history[]`.

## Remote (`sys_log_router`, root key `MODULE_ID_LOG` = 0x2A)

Requests are `[0x2A][dir][op][args]` (`0` read, `1` write). Op `0x02` is permanently unused.

| Op | Side | Request | Response |
|---|---|---|---|
| `0x01` `LOG_OP_CONFIG` | RW | `–` (R) / `sys_log_cfg_t` 5 B (W) | `sys_log_cfg_t` 5 B (R) / `–` (W) |
| `0x03` `LOG_OP_ERRORS` | R | `sys_log_errors_args_t` `{u8 index}` | `[u8 total][record]` (`[u8 total]` when `index >= total`) |
