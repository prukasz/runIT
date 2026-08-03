# System Actions (sys_actions)

IMPORTANT: If you modify the source files in this component (sys_actions.h or sys_actions.c), you must update this documentation to reflect the changes.

## Overview

`sys_actions` is the single mechanism for both **preconfiguration** (recorded control packets, replayed on demand) and **hardcoded system behavior** (what used to be a separate `sys_states` component). An action is a caller-chosen `action_id` in a single unified space, **`0..SYS_ACTIONS_ID_SPACE-1` (0-63)**, and can carry either or both of:

* A **recorded blob** — an ordered list of raw frames (exactly the bytes `sys_interface_decode()` would have received over the wire, class byte included), persisted in NVS. Invoking replays every stored frame back through `sys_interface_decode()`, in order, as if a peer had just sent them.
* A **bound static function** — a hardcoded C function (`action_static_func_t`), for action ids `0..SYS_ACTIONS_STATIC_SLOTS-1` (16) only. This is what a `sys_states` "base action" used to be (e.g. `sys_device_freeze_all()`); `sys_actions_init()` registers the five built-in system sweeps onto ids 1-5 (see [Static Actions](#static-actions)), and more slots are free for anything else that wants one via `sys_actions_bind_static()`.

`sys_actions_invoke(action_id)` runs the bound static function first (if any), then replays the recorded blob (if any) — either alone is a complete, valid action; `ERR_ACTION_NOT_FOUND` only if neither exists.

Two motivating cases:

* **Boot-time device attachment.** A board ships with, say, 3 hardcoded devices configured by `runit_start()`'s static config calls. A user wants 2 more devices attached and configured at boot, ready alongside VM code, *without* a firmware change. They build an action (record it once, or seed it via direct calls) containing the IO/power packets those devices need, then it's saved as **action id 0** — the one action `sys_actions_init()` always invokes unconditionally at boot, right after registering its own class. No firmware change, no re-flash.
* **Hardcoded system sweeps.** Action ids 1-5 always run a hardcoded device sweep (freeze/resume/suspend/reset/hard-reset) via their bound static function — see [Static Actions](#static-actions). A caller can still attach a recorded blob to the same id (e.g. action 1: freeze devices *and* also send a notification packet) since a static function and a blob aren't exclusive.

Two integration points make recording "non-invasive" against [[SYS_INTERFACE.MD]]:
1. A generic **frame tap** — `sys_interface_tap_enable()` turns on a small ring buffer the RX receiver task pushes every live, decoded frame into; sys_actions drains it on its own schedule via a dedicated polling task (`sys_actions_tap_task`, `sys_interface_tap_poll()` every `SYS_ACTIONS_TAP_POLL_MS`), rather than running its recording logic inline inside the receiver task. sys_actions is just one possible consumer of the tap buffer.
2. A generic **RX suspend/resume** pair (`sys_interface_suspend_rx()`/`_resume_rx()`) — while suspended, the RX receiver drains nothing at all, so a replay can't interleave with live traffic.

### Directory Map
```
components/system/sys_actions/
├── CMakeLists.txt
├── SYS_ACTIONS.MD          # This documentation
├── sys_actions.c           # Load-on-demand NVS storage, static-func table, recording tap, boot action
└── include/
    ├── sys_actions.h        # SYS_ACTIONS_CLASS_HEADER, API declarations
    └── sys_error_actions.h  # sys_actions owner map + error tag map
```

The control-packet decoder table (`dec_sys_actions.h`, class `0x03`) lives in `components/codecs/decoders/` — see [[CODECS.MD]].

---

## Storage Model: Load-On-Demand, One Action At A Time

There is no resident in-RAM list of actions. NVS (namespace `"sys_actions"`, key `"act_<id>"` per action, e.g. `"act_0"`, `"act_63"`) is the only durable storage, and every entry point loads exactly the one action it needs into a freshly-allocated buffer, does its work, and frees it:

```c
typedef struct sys_action_t {
  size_t blob_size;
  uint8_t blob[];  // [u16 len][frame bytes]... repeated, packed contiguously
} sys_action_t;
```

`blob` is sized to fit exactly (`malloc(sizeof(sys_action_t) + blob_size)` / `realloc()` on append) — no upfront capacity reservation, no slack, nothing to "fill up".

* **`sys_actions_invoke(id)`** reads `id`'s blob fresh from NVS, replays it, frees it. Never touches anything else in RAM.
* **`sys_actions_append_packet(id, frame, len)`** is one atomic load-modify-save: read whatever's currently under `id` (empty if nothing's stored yet — not an error for this call), append the new `[len][frame]` record, write the whole blob straight back to NVS, free the RAM copy. There is no separate "persist" step — every call is durable the moment it returns.
* **Recording** (`sys_actions_record_start`/`_stop`) is the one place RAM briefly holds a growing blob across multiple calls: only **one action can be actively recording at a time** — a single `static sys_action_t* s_recording` plus `s_recording_id`, guarded by `s_actions_mutex` since the tap-polling task appends to it concurrently with whatever task calls `record_start()`/`record_stop()`. `record_start(id)` fails with `ERR_ACTION_RECORDING_BUSY` if a different action is already recording; otherwise it loads `id`'s existing blob from NVS as the starting point (so recording extends rather than clobbers). `record_stop(id)` (only if it matches the currently-recording id) writes the accumulated blob to NVS once and frees it — this **is** the persist step, there's no separate save call.
* **Not found** is handled the same way everywhere a load happens: `ESP_ERR_NVS_NOT_FOUND` from `nvs_get_blob()` means "start from empty" for load-then-grow call sites (`append_packet`, `record_start`), or `ERR_ACTION_NOT_FOUND` for call sites where the action must already exist (`sys_actions_invoke()`, when no static function is bound either).

---

## Wire Protocol (class `0x03`)

| Packet | Byte | Payload | Calls |
| :--- | :--- | :--- | :--- |
| Record | `0x01` | `uint8_t action_id` | `sys_actions_record_start(action_id)` |
| Stop | `0x02` | `uint8_t action_id` | `sys_actions_record_stop(action_id)` (persists to NVS) |
| Remove | `0x03` | `uint8_t action_id` | `sys_actions_remove(action_id)` |
| Execute | `0x04` | `uint8_t action_id` | `sys_actions_invoke(action_id)` — test-only wire exposure, see below |

Example: `03 01 05` (record action 5).

`sys_actions_invoke()` itself is still meant to be driven from a callback route, application code, or `sys_actions_init()`'s boot action — invoking is not something a real client normally triggers directly over the wire. The `Execute` packet exists purely so a recorded/bound action can be fired on demand from the test GUI without waiting for its real trigger. There is still no wire-exposed `bind_static` packet — binding a hardcoded function stays a boot-time C call (`register_default_static_actions()`).

---

## Recording & Seeding

`sys_actions_record_start(id)` marks `id` as the (sole) currently-recording action, starting from whatever's already stored under it in NVS. From then on, `sys_actions_tap_task` appends every frame of any class **except `SYS_ACTIONS_CLASS_HEADER` itself** (via `sys_actions_on_frame()`, called from the tap-polling task rather than inline from the RX receiver) into the recording buffer — that exclusion is what stops a live "stop recording" command from being captured into the action it was meant to stop. `sys_actions_record_stop(id)` clears the flag and persists the accumulated buffer to NVS in one call — recording durably "just works" with no separate save step.

`sys_actions_append_packet(id, frame, len)` is the lower-level primitive both the tap and the direct "system call" seeding path share conceptually, except append_packet persists immediately on every call (load-modify-save against NVS) rather than accumulating in a long-lived buffer — useful for seeding a handful of packets from application code without going through record/stop.

`sys_actions_remove(id)` erases one action's NVS blob. `sys_actions_remove_all(void)` erases every blob in the `"sys_actions"` namespace — found by iterating the namespace with `nvs_entry_find_in_handle()` rather than assuming anything about what's in RAM, since NVS may hold actions from a previous session. It restarts the NVS iteration after each erase (erasing a key can invalidate an in-progress iterator) rather than trying to delete while iterating; with at most `SYS_ACTIONS_ID_SPACE` (64) actions this is cheap. If `remove`/`remove_all` targets the action currently recording, that recording simply continues in RAM until its own `record_stop` re-writes it — an accepted edge case, not actively guarded against.

---

## Static Actions

Action ids `0..SYS_ACTIONS_STATIC_SLOTS-1` (16) may have a hardcoded C function bound via:

```c
err_h sys_actions_bind_static(uint8_t action_id, action_static_func_t fn, void* arg);
```

`sys_actions_invoke(id)` runs the bound function (with its registered `arg`) before replaying any recorded blob for the same id, and stops immediately if it fails — same stop-at-first-failure convention the blob replay itself uses.

`sys_actions_init()` registers the five sweeps that used to be `sys_states`' built-in "base actions" onto ids **1-5**, via an internal `register_default_static_actions()`:

| Action id | Static function | Notes |
| :--- | :--- | :--- |
| `0` | *(none by default)* | Boot action — blob only unless something binds a static function to it later. |
| `1` | `sys_device_freeze_all()` | Freezes every ready device. Does **not** change device `state` — see the freeze/suspend note below. |
| `2` | `sys_device_resume_all()` **then** `sys_device_sync_all()` | Normal operation. Both calls are needed: `resume_all()` only lifts a *suspend*, `sync_all()` only lifts a *freeze* — see [[SYS_DEVICE.MD]]. |
| `3` | `sys_device_suspend_all()` | Suspends every ready device. |
| `4` | `sys_device_reset_all()` | Resets every ready device. |
| `5` | `sys_device_uninstall_all()` | Uninstalls and frees every registered device. Destructive — nothing is left in the registry afterward. |
| `6-15` | *(free)* | Available for `sys_actions_bind_static()`. |
| `16-63` | *(not eligible)* | Blob-only — `sys_actions_bind_static()` rejects these with `ERR_INVALID_VAL_UI32`. |

A caller can still attach a recorded blob to any of ids 1-5 (e.g. action 1: freeze devices *and* send a notification packet) — a static function and a blob are independent and both run on invoke.

**No "current mode" tracking.** Unlike the removed `sys_states` component, invoking an action does not record anything as "current" — actions are fire-and-forget; `sys_actions_invoke()` just returns success or the first failure.

---

## Invoking

```c
err_h sys_actions_invoke(uint8_t action_id);
```

1. `action_id` must be `< SYS_ACTIONS_ID_SPACE` (64), else `ERR_INVALID_VAL_UI32`.
2. If `action_id < SYS_ACTIONS_STATIC_SLOTS` and a function is bound, it runs first; a failure returns immediately.
3. Whatever's stored under `action_id` in NVS (if anything) is loaded and replayed: `sys_interface`'s RX receiver is suspended for the duration (`sys_interface_suspend_rx()`/`_resume_rx()`) so live traffic cannot interleave with it, each stored frame is fed through `sys_interface_decode()` in order, and replay stops at the first frame whose decode fails.
4. `ERR_ACTION_NOT_FOUND` only if **neither** step 2 nor step 3 did anything — a static-function-only or blob-only action is equally valid.

### Why RX gets suspended
Without suspending, a replayed frame and a genuinely-arriving live frame could interleave through `sys_interface_decode()`. While suspended, the RX receiver drains nothing at all - live frames simply wait in their source's own buffer rather than being dropped or decoded - so replay always has `sys_interface_decode()` to itself. Replay is a *direct* `sys_interface_decode()` call, never routed through the RX receiver task, so it was never tapped in the first place even before suspend was involved - see [[SYS_INTERFACE.MD]].

---

## Boot Action (id 0)

`sys_actions_init()` — after registering its class, enabling the tap, starting its polling task, and registering the built-in static functions on ids 1-5 — always calls `sys_actions_invoke(0)`. If nothing is bound or stored under action 0 yet (`ERR_ACTION_NOT_FOUND`), that's the normal first-boot state and is swallowed, not propagated. Any *other* failure (a stored frame that fails to decode) is returned from `sys_actions_init()` so `runit_start()`'s `CHECK_AND_LOG` surfaces it in the boot log — but does not abort the rest of boot, matching every other `CHECK_AND_LOG` call there.

This is the mechanism for "board configures its extra devices at boot, alongside the hardcoded static config" — build the action once (record it, or seed it via `sys_actions_append_packet()`), and it replays automatically on every subsequent boot with no firmware change.

---

## Design Notes & Known Limitations

* **Boot ordering.** `sys_actions_init()` must run after `sys_interface_init()` and **before** `sys_interface_bind_ble_rx()` — class registration is boot-only, not safe against a running RX receiver. `runit_start()` calls it in that slot.
* **Concurrency.** Only the single active-recording buffer (`s_recording`) is shared mutable state across tasks (the tap-polling task appends, another task can call `record_stop()`), guarded by `s_actions_mutex` (`R_MUTEX_DEFINE`, constructor-created per the [[runit]] skill's static-allocation convention). A single `nvs_handle_t` is shared across tasks without an extra lock, since the NVS library serializes internally.
* **Known race in concurrent NVS access.** Two concurrent calls that both load-modify-save the same `action_id` (e.g. `append_packet` racing `remove`) can clobber each other, since each call releases any lock before doing NVS I/O (NVS I/O and `sys_interface_decode()` replay must not run while holding a lock a re-entrant path could try to take again). Not addressed in this version.
* **`sys_actions` REQUIRES `sys_device`, `sys_interface`, and `codecs` for real** (genuine link dependencies — `sys_actions.c` calls `sys_device_*_all()` for its built-in static functions, `sys_interface_decode()`/`register_class()`/etc., and includes `dec_sys_actions.h`).

---

## API Summary

```c
err_h sys_actions_init(void);

err_h sys_actions_bind_static(uint8_t action_id, action_static_func_t fn, void* arg);

err_h sys_actions_remove(uint8_t action_id);
err_h sys_actions_remove_all(void);
err_h sys_actions_append_packet(uint8_t action_id, const uint8_t* frame, size_t len);

err_h sys_actions_record_start(uint8_t action_id);
err_h sys_actions_record_stop(uint8_t action_id);   // persists to NVS

err_h sys_actions_invoke(uint8_t action_id);        // reads from NVS
```

---

## Error Ownership

`OWNER_SYS_ACTIONS_BASE` (`0xAA00`) tags every error `sys_actions.c` raises directly. The decoder table's own errors are tagged `OWNER_DEC_SYS_ACTIONS` (`0xA611`, defined in `sys_error_interface.h`).

| Tag | Payload | Raised when |
| :--- | :--- | :--- |
| `ERR_ACTION_NOT_FOUND` | `uint8_t action_id;` | `sys_actions_invoke()` on an id with neither a bound static function nor anything in NVS. |
| `ERR_ACTION_RECORDING_BUSY` | `uint8_t action_id;` | `sys_actions_record_start()` while a different action is already recording. |
