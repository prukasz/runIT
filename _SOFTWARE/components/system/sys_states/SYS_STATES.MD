# System State Manager (sys_states)

IMPORTANT: If you modify the source files in this component (sys_states.h or sys_states.c), you must update this documentation to reflect the changes.

## Overview
The `sys_states` component is a thin system-wide operating-mode layer on top of [[SYS_DEVICE.MD]]'s registry sweeps. It does not track any hardware state itself — entering a state runs one static action (almost always a `sys_device_*_all()` sweep across every registered device) and, only if that action succeeds, records the state as current. It is a mode switch, not a supervisor: nothing polls it, and it does not itself decide when to transition.

### Directory Map
```
components/system/sys_states/
├── CMakeLists.txt
├── SYS_STATES.MD          # This documentation
├── sys_states.c           # Static action table + sys_states_enter/get_current/get_name
└── include/
    ├── sys_states.h        # sys_state_e enum and API declarations
    └── sys_error_states.h  # sys_states owner map (SYS_STATES_OWNER_MAP)
```

---

## Architecture & Concept

`sys_states.c` uses the same "static table of functions indexed by an enum" shape as [[SYS_CALLBACKS.MD]]'s route table, except direct index-to-action rather than bit-tested:

```c
static const sys_state_action_fn s_state_actions[SYS_STATE_MAX] = {
    [SYS_STATE_FROZEN] = sys_states_action_frozen,
    [SYS_STATE_RESUME] = sys_states_action_resume,
    ...
};
```

`sys_states_enter(state)` looks up `s_state_actions[state]`, runs it, then calls `sys_actions_invoke_for_state(state)` (see [[SYS_ACTIONS.MD]]) to run whatever actions have been bound to `state`, and only updates `s_current_state` (module-static, starts at `SYS_STATE_NONE`) if **both** succeeded. A failure at either step leaves the current state unchanged — there's no partial/undefined state.

### States & Their Actions

| State | Action | Notes |
| :--- | :--- | :--- |
| `SYS_STATE_NONE` | *(none)* | Initial value before the first `sys_states_enter()` call. Not a valid target — no registered action, so entering it returns `ERR_BASE_NOT_SUPPORTED`. |
| `SYS_STATE_FROZEN` | `sys_device_freeze_all()` | Freezes every ready device. Does **not** change device `state` — see the freeze/suspend note below. |
| `SYS_STATE_RESUME` | `sys_device_resume_all()` **then** `sys_device_sync_all()` | Normal operation. Both calls are needed: `resume_all()` only lifts a *suspend*, `sync_all()` only lifts a *freeze* — see [[SYS_DEVICE.MD]]. |
| `SYS_STATE_SUSPENDED` | `sys_device_suspend_all()` | Suspends every ready device. |
| `SYS_STATE_RESET` | `sys_device_reset_all()` | Resets every ready device. One-shot — does not auto-transition to `SYS_STATE_RESUME` afterward. |
| `SYS_STATE_HARD_RESET` | `sys_device_uninstall_all()` | Uninstalls and frees every registered device. One-shot, and destructive — nothing is left in the registry afterward. |
| `SYS_STATE_ONLINE` | dummy | Logs only; no action implemented yet. |
| `SYS_STATE_OFFLINE` | dummy | Logs only; no action implemented yet. |
| `SYS_STATE_CONNECTION_LOST` | dummy | Logs only; no action implemented yet. |
| `SYS_STATE_EMERGENCY` | placeholder | Logs only; real response actions (e.g. freezing/suspending safety-critical devices) are a `TODO` in `sys_states_action_emergency()` — **this is exactly what `sys_actions` bindings are for**, see below. |

### Bound Actions

After a state's own base action succeeds, `sys_states_enter()` calls `sys_actions_invoke_for_state(state)`, which replays every action bound to `state` via `sys_actions_bind_state()` (up to `SYS_ACTIONS_MAX_BOUND_PER_STATE` = 4 per state, persisted to NVS so bindings survive a reboot). This is what gives a placeholder action like `SYS_STATE_EMERGENCY`'s real teeth without `sys_states` itself needing to know anything about specific devices: bind an action containing e.g. a `sys_vreg_set_enable` packet to `SYS_STATE_EMERGENCY`, and entering that state actually disables the regulator. See [[SYS_ACTIONS.MD]] for the binding wire protocol and NVS layout.

`sys_actions_invoke_for_state()` stops at the first bound action that fails, and that failure propagates out of `sys_states_enter()` exactly like a failing base action would — `s_current_state` is only updated once both the base action and every bound action have succeeded.

### Design Notes

* **No auto-transition.** `SYS_STATE_RESET` and `SYS_STATE_HARD_RESET` are one-shot sweeps represented as ordinary states: after either succeeds, `sys_states_get_current()` reports `SYS_STATE_RESET`/`SYS_STATE_HARD_RESET`, not `SYS_STATE_RESUME`. Call `sys_states_enter(SYS_STATE_RESUME)` explicitly for that. This was a deliberate choice to keep the module mechanical (run-action-then-record) rather than encode a state machine's transition graph; revisit if a caller needs "reset and immediately treat as resumed" as a single call.
* **Freeze/suspend orthogonality is not sys_states' problem to solve** — it's inherent to `sys_device` (see [[SYS_DEVICE.MD]]'s note). `SYS_STATE_RESUME`'s action accounts for it by calling both sweeps; any future state that needs to "undo" a frozen-or-suspended mix should do the same.
* **Not wired into `runit_start()` yet.** No board currently calls `sys_states_enter()` at boot; the module starts at `SYS_STATE_NONE` until something calls it.
* **`sys_states` REQUIRES `sys_actions` for real** (a genuine link dependency — `sys_states.c` calls `sys_actions_invoke_for_state()`). The reverse is not true: `sys_actions.h` references `sys_state_e` as a type only, resolved via a plain include-dir path rather than a component `REQUIRES`, specifically to avoid a circular component dependency — see the note in [[SYS_ACTIONS.MD]].

---

## API

```c
typedef enum sys_state_e {
  SYS_STATE_NONE = 0,
  SYS_STATE_FROZEN,
  SYS_STATE_RESUME,
  SYS_STATE_SUSPENDED,
  SYS_STATE_RESET,
  SYS_STATE_HARD_RESET,
  SYS_STATE_ONLINE,
  SYS_STATE_OFFLINE,
  SYS_STATE_CONNECTION_LOST,
  SYS_STATE_EMERGENCY,
  SYS_STATE_MAX,
} sys_state_e;

err_h sys_states_enter(sys_state_e state);
sys_state_e sys_states_get_current(void);
const char* sys_states_get_name(sys_state_e state);
```

* **`sys_states_enter(state)`**: Runs `state`'s action, then its bound actions (see above); returns `NULL` on success, `ERR_INVALID_VAL_UI32` if `state >= SYS_STATE_MAX`, `ERR_BASE_NOT_SUPPORTED` if `state` has no registered action (only `SYS_STATE_NONE` today), or whatever the underlying `sys_device_*_all()` call or a bound action returned on failure.
* **`sys_states_get_current()`**: Last state successfully entered.
* **`sys_states_get_name(state)`**: Name string for logging, `"UNKNOWN"` if out of range.

---

## Error Ownership

Every error raised in `sys_states.c` is tagged `OWNER_SYS_STATES_BASE` (`0xA900`) — the whole file uses one owner rather than per-function owners, since `sys_states_enter()` is the only function that can actually fail. See `sys_error_states.h` for `SYS_STATES_OWNER_MAP`. No dedicated error tags were added; `sys_states` only raises base tags (`ERR_INVALID_VAL_UI32`, `ERR_BASE_NOT_SUPPORTED`) and propagates whatever `sys_device` or `sys_actions` raises.
