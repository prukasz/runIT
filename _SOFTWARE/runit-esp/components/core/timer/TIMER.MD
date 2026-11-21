# board_timer — the board's timers

Component `components/core/timer/`. One place that runs code later or every so often: a driver's ramp or poll, a module's timeout. Code takes a timer here, never an esp_timer or a FreeRTOS timer of its own, so the platform under it is swapped in one place and the host tests run it on their FreeRTOS stand-ins. A device's timer is `sys_device_timer` (its expiry is a step of the device, `SYS_DEVICE.MD`), built on this one.

## Files

| File | Role |
|---|---|
| `board_timer.h` | `board_timer_t`, the C API |
| `board_timer.c` | The running list and the timers' task |
| `Kconfig` | The task's priority and stack |
| `host/` | Host tests: `python test_host/run.py timer` from `runit-esp/` (HOST_TESTS.MD) |

## Use

```c
typedef struct { board_timer_t timer; ... } module_t;

static void on_timeout(void* ctx) { ... }        // the timers' task

board_timer_init(&m->timer, on_timeout, m);      // set up, stopped
board_timer_start(&m->timer, 10);                // every 10 ms; the same period again keeps the phase
board_timer_once(&m->timer, 500);                // once, 500 ms from now
board_timer_stop(&m->timer);                     // before the struct is freed
board_timer_running(&m->timer);                  // started and not stopped (a once that has fired: stopped)
```

- **A timer is a `board_timer_t` in its owner's struct**: no table, so no capacity and no error; the calls return nothing. Any task, not an ISR.
- **The callbacks run in one task**, `CONFIG_BOARD_TIMER_TASK_PRIO` (7: as the SYS lane of the events, above the DEVICE lane, below the urgent one) with `CONFIG_BOARD_TIMER_TASK_STACK` (4096 B), one after another. Never in an ISR nor on the esp_timer task: a slow callback (I2C) holds up only the other board timers. It may block on I/O, never sleep, and never wait long for a lock (a device's step takes the device's lock only when it is free). The task starts with the first timer started.
- **Fair**: of the timers due, the one that has waited longest runs first, so a callback slower than its own period (due again when it ends) does not starve the others.
- **Time** is the RTOS tick (1 ms). A callback that runs late skips the periods it missed; they are not run in a burst. A callback that needs the real time between its calls reads the clock (`board_time_uptime_ms`).
- **Stop**: no new call starts after `board_timer_stop()`, but one already started runs to its end. So a callback whose owner can go away (a device) takes an ID as its ctx, not a pointer.
- **Start again**: the same period on a running timer changes nothing (its phase stays), so a driver may call `board_timer_start()` on every step; another period, or a once, starts it from now.
- **The step**: `BOARD_TIMER_STEP_MS` (10 ms) is the period of the timers that count time in steps (the router's call timeouts, the sessions' pings, a ramp).

## Inside

The running timers are a list in no order (there are few), under a spinlock held for short walks only. Under the lock the task picks the due timer that has waited longest, moves it on (its next time, or out of the list for a once), then calls its callback without the lock and looks at the list again (the callback may have stopped or started timers). With nothing due it sleeps until the nearest time; a start wakes it.

## Open

- A deterministic stand-in for the host tests of the components that use timers (a test advances the clock and the due callbacks run in its thread), added when the first such test needs it.
