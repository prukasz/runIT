# sys_device_timer — device-owned timers (for agents)

Folder `components/sys/sys_device_timer/`. Wraps [`timer`](../../core/timer/TIMER.MD) (`board_timer_t`) as a resource of a device in [`sys_device`](../sys_device/SYS_DEVICE.MD) so expiry invokes one of the driver's own ops as a device step and teardown stops the timer before the driver struct is freed.

## Files

| File | Role |
|---|---|
| `sys_device_timer.h` | `sys_device_timer_t`, `sys_device_timer_init`, inline `sys_device_timer_start`, `sys_device_timer_stop` |
| `sys_device_timer.c` | Resource release (`board_timer_stop`), `on_expiry` callback (`sys_device_target_unpack` + `sys_device_step`), `sys_device_timer_init` |
| `CMakeLists.txt` | Requires `sys_device` and `timer`; privately `errors` |
| `AGENTS.md` | Rules for changing this folder |

## API

```c
typedef struct {
  sys_device_res_t res;
  board_timer_t timer;
} sys_device_timer_t;

MUST_USE err_h sys_device_timer_init(uint8_t owner, sys_device_timer_t* t, uint16_t op);
static inline void sys_device_timer_start(sys_device_timer_t* t, uint32_t period_ms);
static inline void sys_device_timer_stop(sys_device_timer_t* t);
```

- `sys_device_timer_init` checks `(op >> 8) == SYS_DEVICE_CONTRACT_OWN` (else `ERR_DEV_BAD_ARG`), initializes the underlying `board_timer_t` with `sys_device_target_pack((sys_device_target_t){.id = owner, .gen = sys_device_gen(owner), .op = op})`, and registers `t->res` via `sys_device_res_add(owner, &t->res, release, t)`.
- Call `sys_device_timer_init` from `create()` (when the driver holds `owner`'s lock). Because `t->res` is registered in the device's resource stack, `board_timer_stop(&t->timer)` runs automatically on `REMOVE` (after `suspend()`) and when `create()` fails, before the driver struct is freed.
- Drivers pick an own op with `SYS_DEVICE_OP_LOCAL` and `cmd_none_t` args/answer.

## Step delivery

- On expiry in the timers' task, `on_expiry` calls `err_report(sys_device_step(owner, gen, op))`.
- `sys_device_step` uses a non-blocking try-lock (`sys_device_slot_try_lock`) and writes `op` with empty args:
  - skipped cleanly (returns `NULL`) if the slot is busy, suspended, gated by a running `SUSPEND` sweep, removed, or recreated with a new `gen` (so a busy owner does not hold up other timers on its own lock; note that nested calls to dependencies inside a step still wait up to `CONFIG_SYS_DEVICE_LOCK_TIMEOUT_MS`);
  - because a tick may be skipped while the owner's lock is busy, only periodic timers (`sys_device_timer_start`) are offered; the handler stops the timer on completion or persistent failure (`err_report` reports every error);
  - handlers needing elapsed time read `board_time_uptime_ms()` (`SYS_DEVICE.MD`), because steps may be skipped when the device is busy.

## Tests

No dedicated host folder; `board_timer` is tested in `core/timer/host/test_board_timer.c` and `sys_device_step` (busy skip, suspended skip, safe-gate skip, generation check) in `sys_device/host/test_sys_device.c`.
