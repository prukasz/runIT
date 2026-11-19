# sys_log

Read `SYS_LOG.MD` in this folder before changing anything here.

- Wire format, do not renumber or re-layout: `MODULE_ID_LOG` (`0x2A`), `LOG_OPERATIONS` (`0x01` `OP_CONFIG`, `0x03` `OP_ERRORS`; `0x02` stays unused), `sys_log_cfg_t` (5 B), `sys_log_errors_args_t` (1 B), `SYS_LOG_RECORD_MAX` (192 B), and `ERR_SYS_LOG_MAP`. `router/contract_log.json` and `errors/errors_log.json` follow every change.
- Never `err_report` inside `log_vprintf()` or `sys_log_error()`: both run on paths where reporting errors would re-enter `sys_log_error()` and the ports `LOGS` / `ERRORS`.
- Every send out of `LOGS` and `ERRORS` goes through `push()`, guarded by the per-task `s_sending` flag (what is logged or reported while this task sends is dropped) and `NOWAIT`; `log_vprintf()` stays guarded by `!xPortInIsrContext()` too.
- `sys_log_init()` registers `LOGS` and `ERRORS` (their names are module numbers: `MODULE_ID_LOG`, `MODULE_ID_ERRORS`; wire format); `err_set_sink(...)` after it, before opening the COBS console UART (`mirror_serial = 0` keeps text logs out of binary COBS frames). `ERRORS` passes on what it takes with `send_except(from)`: it is never linked with another module port that passes on.
