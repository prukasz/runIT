# System Actions (sys_actions)

IMPORTANT: If you modify the source files in this component (sys_actions.h or sys_actions.c), you must update this documentation to reflect the changes.

## Overview

`sys_actions` records sequences of inbound control packets ("actions") into NVS and replays them on demand. An action is a caller-chosen `action_id` (`uint8_t`, 0–255) plus an ordered list of raw frames — exactly the bytes `sys_interface_decode()` would have received over the wire, class byte included. Invoking an action feeds every stored frame back through `sys_interface_decode()`, in order, as if a peer had just sent them.

**Actions are the universal mechanism for preconfiguration and device setup that doesn't want to be baked into firmware.** Two motivating cases:

* **Boot-time device attachment.** A board ships with, say, 3 hardcoded devices configured by `runit_start()`'s static config calls. A user wants 2 more devices attached and configured at boot, ready alongside VM code, *without* a firmware change. They build an action (record it once, or seed it via direct calls) containing the IO/power packets those devices need, then it's saved as **action id 0** — the one action `sys_actions_init()` always loads from NVS and invokes unconditionally at boot, right after registering its own class. No firmware change, no re-flash.
* **Recoverable emergency configuration.** A user wants "emergency" to mean "turn off this one regulator" — built once as an action, persisted to NVS, and **bound to `SYS_STATE_EMERGENCY`** (see [[SYS_STATES.MD]]) via the state-binding map below. Because the binding map and the action are both in NVS, this survives a reboot — the emergency response doesn't have to be re-armed by hand every boot.

Two integration points make recording "non-invasive" against [[SYS_INTERFACE.MD]]:
1. A generic **frame tap** — `sys_interface_tap_enable()` turns on a small ring buffer the RX receiver task pushes every live, decoded frame into; sys_actions drains it on its own schedule via a dedicated polling task (`sys_actions_tap_task`, `sys_interface_tap_poll()` every `SYS_ACTIONS_TAP_POLL_MS`), rather than running its recording logic inline inside the receiver task. sys_actions is just one possible consumer of the tap buffer.
2. A generic **RX suspend/resume** pair (`sys_interface_suspend_rx()`/`_resume_rx()`) — while suspended, the RX receiver drains nothing at all, so a replay can't interleave with live traffic.

### Directory Map
```
components/system/sys_actions/
├── CMakeLists.txt
├── SYS_ACTIONS.MD          # This documentation
├── sys_actions.c           # In-RAM staging + NVS persistence, recording tap, boot action, state bindings
└── include/
    ├── sys_actions.h        # SYS_ACTIONS_CLASS_HEADER, API declarations
    └── sys_error_actions.h  # sys_actions owner map + error tag map
```

The control-packet decoder table (`dec_sys_actions.h`, class `0x03`) lives in `components/codecs/decoders/` — see [[CODECS.MD]].

---

## Storage Model: NVS-Only

Earlier versions of this component distinguished a heap-only "dynamic" action type from a fixed-block "NVS" type. **That distinction is gone** — every action is NVS-backed now; there is only one kind of action. What remains in RAM is a *staging buffer* (`sys_action_t`, a linked-list entry via `utils.h`'s `LL_*` macros, same idiom `sys_ble.c` uses for its own lists) that frames get appended into — while recording, while seeding via a direct call, or while an action is loaded back from NVS for inspection. NVS is the durable, canonical copy; the staging buffer is a working area that must be explicitly persisted (`sys_actions_persist_save()`, or automatically via `sys_actions_record_stop()`) to actually survive a reboot.

Each frame is stored `[u16 len][frame bytes]`, packed contiguously — same wire-adjacent framing the earlier design used, now written straight into an NVS blob rather than a fixed RAM-only block. `sys_actions_invoke()` reads directly from NVS (not the staging buffer) and walks this framing.

### NVS layout
* **Namespace**: `"sys_actions"` — deliberately its own namespace, not shared, so a future `sys_vm` bytecode-persistence feature can claim its own storage with zero key collisions.
* **Action blobs**: key `"act_<id>"` (e.g. `"act_0"`, `"act_255"`), one blob per action id, exactly the appended `[u16 len][frame]...` bytes.
* **State binding map**: key `"bindmap"`, one blob holding the whole `sys_state_e -> action_id[]` table (see below) — small and fixed-size, so it's cheap to persist on every bind.

`nvs_flash_init()` is called defensively in `sys_actions_init()` (same one-line pattern `sys_ble_stack_init()` already uses) — safe to call more than once.

### Why NVS and not a filesystem (LittleFS/SPIFFS)
Actions are small, individually-keyed, fixed-shape blobs (0–255 possible ids) — exactly NVS's shape. VM bytecode (a planned, separate future feature) is a different shape of data: larger, likely fewer instances, and plausibly wanting streaming/append/seek semantics a real filesystem gives more naturally than NVS blob-size limits allow. Decision: **keep actions on NVS**, and give VM code its own filesystem-backed storage (LittleFS, when that's built) rather than migrating actions onto it or forcing VM code into NVS. The two live on different partitions with no shared namespace, so neither constrains the other's design later.

---

## Wire Protocol (class `0x03`)

| Packet | Byte | Payload | Calls |
| :--- | :--- | :--- | :--- |
| Record | `0x01` | `uint8_t action_id` | `sys_actions_record_start(action_id)` |
| Stop | `0x02` | `uint8_t action_id` | `sys_actions_record_stop(action_id)` (persists to NVS) |
| Remove | `0x03` | `uint8_t action_id` | `sys_actions_remove(action_id)` |
| Bind to state | `0x04` | `uint8_t action_id; uint8_t state;` | `sys_actions_bind_state(action_id, (sys_state_e)state)` |

Examples: `03 01 05` (record action 5), `03 04 05 09` (bind action 5 to state `9` = `SYS_STATE_EMERGENCY`).

There is deliberately **no wire-exposed invoke packet** — `sys_actions_invoke()` is a C-level API only (called from a callback route, `sys_states_enter()`, or application code), not a control packet.

---

## Recording & Seeding

`sys_actions_record_start(id)` marks action `id` as recording (auto-creating a `SYS_ACTIONS_DEFAULT_MAX_SIZE`-byte staging buffer if it doesn't exist). From then on, `sys_actions_tap_task` appends every frame of any class **except `SYS_ACTIONS_CLASS_HEADER` itself** (via `sys_actions_on_frame()`, now called from the tap-polling task rather than inline from the RX receiver) to every action currently recording — that exclusion is what stops a live "stop recording" command from being captured into the action it was meant to stop. `sys_actions_record_stop(id)` clears the flag and **persists the staging buffer to NVS**, so recording now durably "just works" without a separate save step.

`sys_actions_append_packet(id, frame, len)` is the lower-level primitive both the tap and the direct "system call" seeding path share — it only touches the staging buffer; call `sys_actions_persist_save(id)` explicitly when seeding this way (there's no implicit auto-save outside of `record_stop()`).

`sys_actions_create(id, max_size)` reserves a staging buffer of a specific capacity up front (useful when you know the seeded sequence will be larger than the default 512 bytes).

`sys_actions_remove(id)` deletes one action (staging buffer + NVS blob). `sys_actions_remove_all(void)` is the simple "wipe everything" call: frees every in-RAM staging buffer, then erases every `"act_*"` blob in NVS — found by iterating the namespace with `nvs_entry_find_in_handle()` rather than only walking the in-RAM list, since NVS may hold actions from a previous session that were never touched this boot. It restarts the NVS iteration after each erase (erasing a key can invalidate an in-progress iterator) rather than trying to delete while iterating; with at most 256 actions this is cheap. The `"bindmap"` key is deliberately left alone — see [State Bindings](#state-bindings).

---

## Invoking

```c
err_h sys_actions_invoke(uint8_t action_id);
```

Reads `action_id`'s blob **fresh from NVS** (not the staging buffer, even if one happens to exist with unsaved edits — NVS is the source of truth for what actually gets replayed), suspends the RX receiver, walks the `[u16 len][frame]...` framing calling `sys_interface_decode()` per frame, then resumes the RX receiver **unconditionally** — success or failure. Stops at the first frame whose decode fails (same stop-at-first-failure convention as `sys_device_*_all()`) and returns that error. `ERR_ACTION_NOT_FOUND` if nothing is stored under `action_id`.

### Why RX gets suspended
Without suspending, a replayed frame and a genuinely-arriving live frame could interleave through `sys_interface_decode()` — exactly what an emergency/preconfiguration replay needs to avoid. While suspended, the RX receiver drains nothing at all - live frames simply wait in their source's own buffer rather than being dropped or decoded - so replay always has `sys_interface_decode()` to itself. Also note replay is a *direct* `sys_interface_decode()` call, never routed through the RX receiver task, so it was never tapped in the first place even before suspend was involved - see [[SYS_INTERFACE.MD]].

---

## Boot Action (id 0)

`sys_actions_init()` — after registering its class, enabling the tap and starting its polling task, and loading the state-binding map — always calls `sys_actions_invoke(0)`. If nothing is stored under action 0 yet (`ERR_ACTION_NOT_FOUND`), that's the normal first-boot state and is swallowed, not propagated. Any *other* failure (a stored frame that fails to decode) is returned from `sys_actions_init()` so `runit_start()`'s `CHECK_AND_LOG` surfaces it in the boot log — but does not abort the rest of boot, matching every other `CHECK_AND_LOG` call there.

This is the mechanism for "board configures its extra devices at boot, alongside the hardcoded static config" — build the action once (record it, or seed it via direct calls + `sys_actions_persist_save(0)`), and it replays automatically on every subsequent boot with no firmware change.

---

## State Bindings

```c
#define SYS_ACTIONS_MAX_BOUND_PER_STATE 4

err_h sys_actions_bind_state(uint8_t action_id, sys_state_e state);
err_h sys_actions_invoke_for_state(sys_state_e state);
```

Each state ID - built-in or custom (see [[SYS_STATES.MD]]) - can have up to `SYS_ACTIONS_MAX_BOUND_PER_STATE` (4) actions bound to it; `s_bind_map`'s arrays are sized `SYS_STATES_ID_SPACE` wide (built-ins + every custom slot), not just `SYS_STATE_MAX`, so a custom state binds exactly like a built-in one. `sys_actions_bind_state()` appends to that state's list (a no-op if `action_id` is already bound to it) and persists the **entire** binding map to NVS on every call — bindings are cheap and rare (a setup-time operation), so there's no batching. `ERR_ACTION_STATE_BINDINGS_FULL` if a 5th action is bound to the same state.

For a custom state specifically, the bound action(s) are its *only* behavior - `sys_states_enter()` skips the base-action step entirely for `state >= SYS_STATE_MAX`, so a custom state with nothing bound is a harmless no-op, not a failure.

`sys_states_enter(state)` calls `sys_actions_invoke_for_state(state)` immediately after running that state's own base action (freeze/suspend/etc.) and before marking the state current — so a bound action's failure means the state transition itself fails (matching the "action must succeed before state updates" convention already established there). This is how `SYS_STATE_EMERGENCY` — whose own action is still just a log stub — gets real teeth: bind an action containing e.g. a `sys_vreg_set_enable` packet to it, and entering `EMERGENCY` will actually disable that regulator.

**Type boundary note**: `sys_actions.h` includes `sys_states.h` for the `sys_state_e` type (function signatures only, no calls into `sys_states`). To avoid a circular component dependency — `sys_states.c` genuinely calls `sys_actions_invoke_for_state()`, a real link edge — `sys_actions`'s `CMakeLists.txt` adds `sys_states`'s `include/` as a **plain path**, not a `REQUIRES`, mirroring the exact technique `sys_errors`'s `CMakeLists.txt` already uses for `sys_device`/`sys_io`/etc. Only `sys_states -> sys_actions` is a real (link-level) component dependency; `sys_actions` never links against `sys_states`.

---

## Design Notes & Known Limitations

* **Boot ordering.** `sys_actions_init()` must run after `sys_interface_init()` and **before** `sys_interface_bind_ble_rx()` — class registration is boot-only, not safe against a running RX receiver. `runit_start()` calls it in that slot, followed by `sys_states_init()`.
* **Concurrency.** All access to the in-RAM staging list (`s_actions`) and the binding map (`s_bind_map`) is guarded by one mutex (`R_MUTEX_DEFINE`, constructor-created per the [[runit]] skill's static-allocation convention). Every public function takes the lock exactly once and calls only lock-free internal helpers — never another public function — to avoid a recursive-lock deadlock. A single `nvs_handle_t` is shared across tasks without an extra lock, since the NVS library serializes internally.
* **Known race in `sys_actions_invoke()`/`_persist_save()`.** Both release the RAM lock before doing NVS I/O or replay (replay calls `sys_interface_decode()`, which must not run while holding a lock a re-entrant path could try to take again). A concurrent `sys_actions_remove(action_id)` racing either can free the staging entry out from under it. Not addressed in this version.
* **`sys_actions_invoke()` always reads NVS, never the staging buffer.** If you `sys_actions_append_packet()` without ever calling `sys_actions_persist_save()`, invoking that id still replays whatever (if anything) was last actually saved — this is intentional (NVS is the source of truth) but easy to forget mid-development.

---

## API Summary

```c
err_h sys_actions_init(void);

err_h sys_actions_create(uint8_t action_id, size_t max_size);
err_h sys_actions_remove(uint8_t action_id);
err_h sys_actions_remove_all(void);
err_h sys_actions_append_packet(uint8_t action_id, const uint8_t* frame, size_t len);

err_h sys_actions_record_start(uint8_t action_id);
err_h sys_actions_record_stop(uint8_t action_id);   // persists to NVS

err_h sys_actions_invoke(uint8_t action_id);        // reads from NVS

err_h sys_actions_bind_state(uint8_t action_id, sys_state_e state);
err_h sys_actions_invoke_for_state(sys_state_e state);

err_h sys_actions_persist_save(uint8_t action_id);
err_h sys_actions_persist_load(uint8_t action_id);
```

---

## Error Ownership

`OWNER_SYS_ACTIONS_BASE` (`0xAA00`) tags every error `sys_actions.c` raises directly. The decoder table's own errors are tagged `OWNER_DEC_SYS_ACTIONS` (`0xA611`, defined in `sys_error_interface.h`).

| Tag | Payload | Raised when |
| :--- | :--- | :--- |
| `ERR_ACTION_NOT_FOUND` | `uint8_t action_id;` | `sys_actions_invoke()`/`_persist_save()`/`_persist_load()` on an id with no staging buffer or NVS entry, as applicable. |
| `ERR_ACTION_BLOCK_FULL` | `uint8_t action_id; uint32_t used; uint32_t size;` | Appending would exceed the staging buffer's capacity. |
| `ERR_ACTION_STATE_BINDINGS_FULL` | `uint8_t action_id; uint8_t state;` | `state` already has `SYS_ACTIONS_MAX_BOUND_PER_STATE` actions bound. |
