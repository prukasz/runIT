# sys_log

Read `SYS_LOG.MD` in this folder before changing anything here.

- Wire format, do not renumber or re-layout: `MODULE_ID_LOG` (`0x2A`), `LOG_OPERATIONS` (`0x01` `OP_CONFIG`, `0x03` `OP_ERRORS`; `0x02` stays unused), `sys_log_cfg_t` (5 B), `sys_log_errors_args_t` (1 B), `SYS_LOG_RECORD_MAX` (192 B), and `ERR_SYS_LOG_MAP`. `router/contract_log.json` and `errors/errors_log.json` follow every change.
- Never `err_report` inside `log_vprintf()` or `sys_log_error()`: both run on paths where reporting errors would re-enter `sys_log_error()` and `LOG_STREAM_LOGS` / `LOG_STREAM_ERRORS`. Free transient stream send errors with `err_free()`.
- `log_vprintf()` must remain guarded by `!data_matrix_is_task()` and `!xPortInIsrContext()` so logs emitted inside the matrix's delivery task never re-queue themselves.
- `sys_log_init()` tells the matrix the `LOG_STREAMS`: call it after `data_matrix_init()`, and `err_set_sink(...)` after it, before opening the COBS console UART (`mirror_serial = 0` keeps text logs out of binary COBS frames).
