# time — the board's clock

Component `components/core/time/`. The one place that sets and reads the time. No dependency on other runIT components.

## Files

| File | Role |
|---|---|
| `board_time.h` / `board_time.c` | The API below. The header is not `time.h`: that would shadow libc's `<time.h>` on every include path |
| `CMakeLists.txt` | Component registration (`PRIV_REQUIRES esp_timer`) |
| `AGENTS.md` | Rules for changing this folder |

## Model

No battery clock: the time is a client's gift (`board_time_set`, the sys router's `TIME` write, SYS_SYSTEM.MD), kept over a software restart (the RTC counter runs on) and lost with the power. It is the system clock (`settimeofday`), so code reading the C library's time sees the same one. A clock that was never set counts from 1970 at the start: anything before `BOARD_TIME_SET_AFTER_MS` (2020-01-01 UTC) is "not set".

| Function | Gives |
|---|---|
| `board_time_now_ms()` | UTC ms once set; before that the time since the start (not a date). What a stamp that must always have a value uses (the error record) |
| `board_time_unix_ms()` | UTC ms, 0 while never set |
| `board_time_is_set()` | whether a client set it |
| `board_time_set(unix_ms)` | sets it; false when the system refuses |
| `board_time_uptime_ms()` | the time since the start, monotonic; setting the clock does not move it. Safe in an ISR (the clock functions take a lock and are not) |
| `board_time_at_uptime_ms(up)` | the clock at an earlier `board_time_uptime_ms` reading; for a stamp taken in an ISR, converted in a task |

## Who uses it

- `errors`: the stamp of an error record (`board_time_now_ms`).
- `sys_system`: the `TIME` op (`board_time_set`, `board_time_unix_ms`).
- `sys_event`: `t_ms`, the event's time (`board_time_now_ms`, u64), the same as an error record's. Published from an ISR, where the clock can't be read, it stamps `board_time_uptime_ms` and the event task converts it with `board_time_at_uptime_ms`.
- The VM keeps its own program time (`vm_clock.h`): it stands still while the program is paused or stepped, which this clock never does.
