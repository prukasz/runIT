# System Actions (sys_actions)

IMPORTANT: If you modify the source files in this component (sys_actions.h or sys_actions.c), you must update this documentation to reflect the changes.

## Overview

`sys_actions` is the single mechanism for both **preconfiguration** (recorded control packets, replayed on demand) and **hardcoded system behavior** (what used to be a separate `sys_states` component).

Actions are split into two distinct scopes:
* **Static Actions (Scope `0x00`)**: Hardcoded compile-time C functions (`action_static_func_t`, taking `void`), indexed `1..CONFIG_SYS_ACTIONS_STATIC_SLOTS-1` (default 16). `sys_actions_init()` registers built-in system sweeps on IDs 2-6 (freeze, resume, suspend, reset, hard-reset), and additional slots can be bound at compile time / boot via `sys_actions_bind_static(id, fn)`.
* **Dynamic Actions (Scope `0x01`)**: Runtime packet sequences persisted in NVS (`"act_<id>"` keys), indexed `1..255` (`uint8_t`). Each dynamic action has a strict size limit of **2 KB** (2048 bytes). Frames are recorded from live traffic and replayed through `sys_interface_decode()`.

Naming is hidden from user-facing APIs — actions are exclusively identified and manipulated via `scope` and `id`.

Invocation is performed through a single unified entry point:
```c
err_h sys_actions_invoke(uint8_t scope, uint8_t id);
```

Two integration points make dynamic action recording and replay "non-invasive" against [[SYS_INTERFACE.MD]]:
1. A generic **frame tap** — while capture is active, the RX receiver task pushes live frames into a static ring buffer; sys_actions starts capture with recording and drains it via a dedicated polling task (`sys_actions_tap_task`, `sys_interface_tap_poll()` every `SYS_ACTIONS_TAP_POLL_MS`).
2. A generic **RX suspend/resume** pair (`sys_interface_suspend_rx()`/`_resume_rx()`) — while suspended during dynamic action replay, the RX receiver drains nothing at all, preventing replayed frames from interleaving with live traffic.

### Directory Map
```
components/system/sys_actions/
├── CMakeLists.txt
├── SYS_ACTIONS.MD          # This documentation
├── sys_actions.c           # Load-on-demand NVS storage, tap task, invocation & management APIs
├── sys_actions_static.c    # Default static action implementations
└── include/
    ├── sys_actions.h        # SYS_ACTIONS_CLASS_HEADER, API declarations, scopes
    ├── sys_actions_static.h # Built-in static action slot IDs
    └── sys_error_actions.h  # sys_actions owner map + error tag map
```

---

## Wire Protocol (Class `0x03`)

Class byte `0x03` (`SYS_ACTIONS_CLASS_HEADER`) routes to `dec_sys_actions.h`:

| Packet | Byte | Payload | Calls | Description |
| :--- | :--- | :--- | :--- | :--- |
| Static Invoke | `0x00` | `uint8_t id` | `sys_actions_invoke(0x00, id)` | Invokes static action `id` (`0x03 0x00 YY`) |
| Dynamic Invoke | `0x01` | `uint8_t id` | `sys_actions_invoke(0x01, id)` | Invokes dynamic action `id` (`0x03 0x01 ZZ`) |
| Record Start | `0x02` | `uint8_t id` | `sys_action_record_start(id)` | Starts recording frames into dynamic action `id` |
| Record Stop | `0x03` | *(empty)* | `sys_action_record_stop()` | Stops recording and persists blob under remembered `id` |
| Remove | `0x04` | `uint8_t id` | `sys_action_remove(id)` | Erases dynamic action `id` from NVS |
| Remove All | `0x05` | *(empty)* | `sys_action_remove_all()` | Erases all dynamic actions from NVS |

Frames with class `0x03` are automatically excluded from recording by the tap consumer. Thus, `record_stop` (`0x03 0x03`) is never captured inside the recorded action.

---

## Dynamic Actions & Recording

Only **one action can be actively recorded at a time**.

```c
err_h sys_action_record_start(uint8_t id);
err_h sys_action_record_stop(void);
err_h sys_action_remove(uint8_t id);
err_h sys_action_remove_all(void);
```

* **`sys_action_record_start(id)`**: Remembers `id` as the active recording target and loads any pre-existing NVS blob under `id` into RAM (or initializes an empty buffer). Returns `ERR_ACTION_RECORDING_BUSY` if a recording is already in progress.
* **Recording Tap**: Incoming frames (excluding class `0x03`) are appended into `s_recording`. Appending enforces the **2 KB (2048 bytes)** limit — growing stops if the limit is exceeded.
* **`sys_action_record_stop()`**: Takes no arguments; disables capture, drains all frames already queued in the static tap buffer into the active action, then writes the accumulated blob to NVS namespace `"sys_actions"`, frees RAM, and resets the recording state.
* **`sys_action_remove(id)`**: Erases `"act_<id>"` from NVS.
* **`sys_action_remove_all()`**: Iterates and erases all dynamic action keys from NVS.

---

## Static Actions

Static actions take no arguments:
```c
typedef err_h (*action_static_func_t)(void);
err_h sys_actions_bind_static(uint8_t action_id, action_static_func_t fn);
```

ID `0` is reserved for no action and is rejected by binding, invocation, recording, and removal APIs. Existing NVS keys are not migrated. Stored control packets using old built-in IDs must be updated before replay.

Slots `1..CONFIG_SYS_ACTIONS_STATIC_SLOTS-1` (default 16):
| Action id | Static function | Notes |
| :--- | :--- | :--- |
| `1` | *(unbound by default)* | Boot action (e.g. `runit_at_boot`). Explicitly invoked by application boot. |
| `2` | `sys_device_freeze_all()` | Freezes all devices. |
| `3` | `sys_device_resume_all()` + `sync_all()` | Normal operation resumed. |
| `4` | `sys_device_suspend_all()` | Suspends all devices. |
| `5` | `reset_all()` + `vm_exec_control(VM_EXEC_RESET_TO_START)` + invoke boot action `1` | Resets devices, rewinds VM, and re-applies boot config. |
| `6` | `vm_loader_reset()` + `uninstall_all()` + invoke boot action `1` | Hard reset: unloads VM program, uninstalls devices, re-applies boot config. |
| `7-15` | *(free)* | Available for compile-time or boot-time `sys_actions_bind_static()`. |

---

## Invoking Actions

```c
err_h sys_actions_invoke(uint8_t scope, uint8_t id);
```

1. **Scope `0x00` (Static):**
   - Checks `id < CONFIG_SYS_ACTIONS_STATIC_SLOTS` and `s_static_funcs[id] != NULL`.
   - Runs `s_static_funcs[id]()`. Returns `ERR_ACTION_NOT_FOUND` if not bound.
2. **Scope `0x01` (Dynamic):**
   - Loads blob from NVS under `"act_<id>"`. Returns `ERR_ACTION_NOT_FOUND` if not in NVS.
   - Suspends RX (`sys_interface_suspend_rx()`).
   - Replays stored frames sequentially through `sys_interface_decode()`.
   - Resumes RX (`sys_interface_resume_rx()`), frees blob, and returns status.
3. **Other scopes:** Returns `ERR_INVALID_VAL_UI32`.

---

## Error Ownership

`OWNER_SYS_ACTIONS_BASE` (`0xAA00`) tags errors raised by `sys_actions.c`.

| Tag | Payload | Raised when |
| :--- | :--- | :--- |
| `ERR_ACTION_NOT_FOUND` | `uint8_t action_id;` | `sys_actions_invoke()` on an unbound static ID or non-existent dynamic ID. |
| `ERR_ACTION_RECORDING_BUSY` | `uint8_t action_id;` | `sys_action_record_start()` called while an action is already recording. |

## Callback execution

`sys_actions_init()` registers `sys_actions_invoke()` as the callback action executor. After route handlers return, the callback worker invokes the nonzero dynamic action ID followed by the nonzero static action ID. Errors are reported independently; an error from the user action does not skip the system action. Driver own-function callbacks bypass action dispatch.
