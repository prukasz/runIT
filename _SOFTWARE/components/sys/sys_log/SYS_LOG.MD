# sys_log — log output control

Component `components/sys/sys_log/` (2026-10-10). Owns the ESP_LOG hook (moved from `sys_connector`) and the log options (the three of the legacy `SE_set_logging` plus `send_errors`, `send_logs`), kept as a record (SYS_ACTIONS.MD, `0x01000001`) and applied from the defaults at boot.

| File | Contents |
|---|---|
| `sys_log.h` | `sys_log_cfg_t`, `ERR_SYS_LOG_MAP`, API |
| `sys_log.c` | The `vprintf` hook, apply, load / store. `ERR_MOD_SYS_LOG` = 13. Linked as `WHOLE_ARCHIVE` |
| `sys_log_router.c/.h` | Remote interface, root key `ROUTER_KEY_LOG` = 0x08 |
| `Kconfig` | `SYS_LOG_LINE_MAX` (160): the line buffer of the hook (was `SYS_CONNECTOR_LOG_LINE_MAX`) |

## The options

| Field | Values | Effect |
|---|---|---|
| `level` | 0 .. `CONFIG_LOG_MAXIMUM_LEVEL` (3 = INFO here) | `esp_log_level_set("*", level)`. **0 blocks every ESP_LOG line**, console and logs connector. Above the compile-time ceiling: `ERR_LOG_BAD_LEVEL{level, max}` (a level IDF compiled out would silently do nothing; the legacy code accepted up to VERBOSE) |
| `mirror_serial` | 0 / 1 | 1: a line goes to the console (UART / USB-Serial-JTAG) and the logs connector. 0: only to the logs connector (BLE) |
| `send_errors` | 0 / 1 | 1: a reported error chain is sent as a record on the errors stream. 0: no record is sent (`sys_log_send_errors()` gates the error sink in `runit_error_policy.c`); the command still gets its error response |
| `send_logs` | 0 / 1 | 1: every log line is also sent as text to the logs connector (BLE). 0: lines stay on the console |
| `trace_errors` | 0 / 1 | 1: a reported error chain is also printed (`err_log`). 0: no chain lines (`err_log_enable(false)` in `errors`); the chain still becomes an error record on the errors stream |

What no option can silence: the command link (`#R:` lines are written to the console directly), the error records, and the ROM / bootloader output before the application runs.

Defaults after a reset: INFO (`CONFIG_LOG_DEFAULT_LEVEL`), mirror on, trace on, send errors on, send logs on (what the legacy board set at boot).

## The hook

`sys_log_init(connector)` installs `esp_log_set_vprintf`. Per line: the console (if `mirror_serial`), then, skipping lines from ISRs, before the scheduler runs and from the connector task itself (they would loop), a copy without ANSI colours and the line end, cut to `SYS_LOG_LINE_MAX`, to the logs connector (`sys_connector_send`: never blocks, a full queue drops the line). The line is formatted on the logging task's stack. Log version 1 (`CONFIG_LOG_VERSION_1`): one `vprintf` per line.

## Boot

`streams` (connector, error sink) → `log` (`sys_log_init(SYS_CONNECTOR_LOGS)`: hook, then the defaults). Lines logged between `streams` and `log` go to the console only;

SET with a level above the ceiling is refused (`ERR_LOG_BAD_LEVEL`, nothing changes).

## Remote (`ROUTER_KEY_LOG` = 0x08)

| Op | Request | Response |
|---|---|---|
| `0x01` SET | `{u8 level, u8 mirror_serial, u8 trace_errors, u8 send_errors, u8 send_logs}` (flags 0 / 1, else `ERR_CMD_BAD_ARG`) | – (applied at once; not stored: save the SET frame as the record) |
| `0x02` GET | – | the same 5 bytes, the options in force |

Example: level 0 on the UART, `07 08 01 00 01 01 01 01`.

SET applies the options at once and stores nothing. The app keeps them as the record `0x01000001` (`SYS_ACTIONS_ID_LOG`): the SET frame `[0x08][0x01][5 bytes]` saved with upload or recording (SYS_ACTIONS.MD); whoever starts the project runs it. After a reset the defaults apply.

## Changes from legacy (`SE_set_logging`)

- The first three options and their meaning are the same; they can be kept as a record now (legacy applied them at boot from constants) and are read back with GET.
- The level is checked against `CONFIG_LOG_MAXIMUM_LEVEL`, not VERBOSE.
- `trace_errors` stops the text of a chain only; legacy `se_log_error_chain` also skipped the sink's log line copy. Here the text goes through ESP_LOG, so the logs connector gets it too (when `mirror_serial` or not) unless it is off.
- No sink / log task / repeat suppression: the error chain output is the records on the errors stream (ERRORS.MD) and, optionally, the lines.

## Tested

QEMU (`router_tests.py` `test_log`): defaults, refused values change nothing, each option separately (chain lines on / off, console silent, level 0 silent, the record always sent), the options survive a restart.
