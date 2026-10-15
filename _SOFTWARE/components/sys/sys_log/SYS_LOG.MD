# sys_log — log output control

Component `components/sys/sys_log/`. Owns the ESP_LOG hook and the log options: five settings, applied at boot from the defaults and changed with SET. The app keeps them as the settings record `SYS_ACTIONS_ID_LOG` (`0x01000001`, SYS_ACTIONS.MD).

| File | Contents |
|---|---|
| `sys_log.h` | `sys_log_cfg_t`, `ERR_SYS_LOG_MAP`, API |
| `sys_log.c` | The `vprintf` hook, apply, defaults. `ERR_MOD_SYS_LOG` = 13. Linked `WHOLE_ARCHIVE`: the error tag is registered by a constructor |
| `sys_log_router.c/.h` | Remote interface, root key `ROUTER_KEY_LOG` = 0x08 |
| `contract_log.json` | The router's descriptor (SET, GET and the options) |
| `Kconfig` | `SYS_LOG_LINE_MAX` (160): the line buffer of the hook |
| `AGENTS.md` | Rules for changing this folder |

## The options

| Field | Values | Effect |
|---|---|---|
| `level` | 0 .. `CONFIG_LOG_MAXIMUM_LEVEL` (3 = INFO here) | `esp_log_level_set("*", level)`. **0 blocks every ESP_LOG line**, console and logs connector. Above the compile-time ceiling: `ERR_LOG_BAD_LEVEL{level, max}`, since a level IDF compiled out would silently do nothing |
| `mirror_serial` | 0 / 1 | 1: a line goes to the console (UART / USB-Serial-JTAG). 0: not to the console; a line still reaches the logs connector if `send_logs` is 1 |
| `send_errors` | 0 / 1 | 1: a reported error chain is sent as a record on the errors stream (ERRORS.MD). 0: no record is sent; `sys_log_send_errors()` gates the error sink in `runit_error_policy.c` |
| `send_logs` | 0 / 1 | 1: every log line is also sent as text to the logs connector (BLE, or any connector bound to it). 0: no line is sent to a connector |
| `trace_errors` | 0 / 1 | 1: a reported error chain is also printed as log lines (`err_log`). 0: no chain lines (`err_log_enable(false)`). Independent of `send_errors` |

The text of a chain goes through ESP_LOG, so with `trace_errors` on it reaches the console and the logs connector like any other line.

What no option can silence: the command link (`#R:` frames from sys_uart, which don't go through ESP_LOG), the error records, and the ROM / bootloader output before the application runs.

Defaults after a reset: level `CONFIG_LOG_DEFAULT_LEVEL` (INFO), mirror on, trace on, send errors on, send logs on.

## The hook

`sys_log_init(connector)` installs `esp_log_set_vprintf` (log version 1: one `vprintf` per line). Per line:

1. The console, if `mirror_serial`.
2. A copy to the logs connector, if `send_logs`, but not from an ISR, not before the scheduler runs, and not from the connector task itself (its own logs would loop). The copy has no ANSI colour sequences and no line end, and is cut to `SYS_LOG_LINE_MAX`. The line is formatted on the logging task's stack. `sys_connector_send` never blocks: a full queue drops the line, silently.

## Boot

`streams` (connector, error sink) → `log` (`sys_log_init(SYS_CONNECTOR_LOGS)`: the hook, then the defaults). Lines logged between `streams` and `log` go to the console only. Boot steps: runit.c.

## Remote (`ROUTER_KEY_LOG` = 0x08)

| Op | Request | Response |
|---|---|---|
| `0x01` SET | `sys_log_cfg_t`: `{u8 level, mirror_serial, trace_errors, send_errors, send_logs}` (the four flags 0 / 1, else `ERR_CMD_BAD_ARG`) | – (applied at once; nothing is stored) |
| `0x02` GET | – | `sys_log_cfg_t`: the options in force |

Example, level 0 with the console and both connector streams on: the command `[08][01][00][01][01][01][01]` (key, op, then the five bytes). The link adds its own frame header (ROUTER.MD).

SET applies the options at once and stores nothing. The app keeps the SET frame as the record `SYS_ACTIONS_ID_LOG` (saved with the upload or the recording, SYS_ACTIONS.MD) and it runs when the project starts. After a reset the defaults apply until then.

## Errors (`ERR_MOD_SYS_LOG` = 13)

`ERR_LOG_BAD_LEVEL {level, max}` HIGH: a level above `CONFIG_LOG_MAXIMUM_LEVEL`. Nothing changes.

## Limits

- **No repeat suppression**: every reported chain is sent (records and, with `trace_errors`, lines) each time.
- A line is cut to `SYS_LOG_LINE_MAX` (160 bytes); the rest is lost on the connector, not on the console.

## Tests

`router_tests.py`, `test_log` (in `.claude/skills/runit-esp/scripts/`) covers the defaults, refused values, each option and the options after a restart.
