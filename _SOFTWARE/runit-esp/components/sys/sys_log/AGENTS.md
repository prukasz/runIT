# sys_log

Read `SYS_LOG.MD` in this folder before changing anything here.

- Wire format, do not renumber or re-layout: `MODULE_ID_LOG` (`0x29`), `LOG_OPERATIONS` (`0x01` `OP_CONFIG`, `0x03` `OP_ERRORS`; `0x02` stays unused), `sys_log_cfg_t` (3 B), `sys_log_errors_args_t` (1 B), and `ERR_SYS_LOG_MAP`. `router/contract_log.json` and `errors/errors_log.json` follow every change.
- Never `err_report` inside `log_vprintf()` or `sys_log_report()`: both run on paths where reporting errors would re-enter `sys_log_report()` and the ports `LOGS` / `ERRORS`.
- Every send out of `LOGS` and `ERRORS` goes through `push()`, which writes the header into the `CMD_HEAD` bytes its caller left free before the data (a line or a record is built in its frame's buffer, never copied) and sends `NOWAIT`; both callers skip while the per-task `s_sending` flag is set (what is logged or reported while this task sends is dropped); `log_vprintf()` stays guarded by `!xPortInIsrContext()` too.
- `sys_log_init()` registers `LOGS` and `ERRORS` (their names are module numbers: `MODULE_ID_LOG`, `MODULE_ID_ERRORS`; wire format); it runs before `runit_data_matrix_setup` opens the console provider (`mirror_serial = 0` keeps text logs out of its binary frames). `ERRORS` passes on what it takes with `send_except(from)`: it is never linked with another module port that passes on.
