# VM Completion Plan

This document turns the VM integration review into an implementation order.
It deliberately excludes block-palette completeness and callback blocks. Work
from top to bottom: priority 0 items can corrupt state or defeat a safety
response; priority 1 items are required for reliable production operation;
priority 2 items complete system and transport integration; priority 3 items
are standalone improvements.

When a task changes `components/VM/core/**/*.c` or `*.h`, update [VM.MD](VM.MD)
and the relevant contract document in the same change.

## Common commands

Run these from the repository root in an ESP-IDF PowerShell environment:

```powershell
idf.py reconfigure
idf.py build
```

For target validation with the existing boot self-tests:

```powershell
idf.py -p <COM_PORT> flash monitor
```

The expected result is a final VM self-test line containing `0 failed`. Before
a production build, disable boot tests and re-enable device initialization in
`components/runit/runit_board_cfg.h`.

Useful review commands:

```powershell
git diff --check
git diff -- components/VM components/runit components/system components/sys_errors components/devices
rg -n "TODO|FIXME" components/VM components/runit components/system components/devices
```

## Priority 1 — reliability and coherent lifecycle

### 7. Add an explicit program validation and commit state

**Problem:** loader state is only EMPTY or OPEN. An incomplete program can be
run; missing block slots are skipped. A failed record may also consume arena
space or bind a partially initialized registry entry, making retry behavior
unclear.

**Fix in:**

- `core/loader/vm_loader.c` and `.h`
- `core/store/vm_store.c` if staging storage is introduced
- `core/exec/vm_exec.c`
- `../codecs/decoders/dec_vm_loader.h`
- `../Python/PythonRunIT/vm/program.py`
- `../runit/selftest/stage_loader.c`

**Required behavior:**

1. Add a COMMITTED/READY state and a wire command that finalizes upload.
2. At commit, verify all declared object, accessor, and block slots expected by
   the program are populated; all accessors resolve; block wiring and spans are
   valid; and `on_error` values are recognized.
3. Reject execution commands unless a program is committed.
4. Make each add-record operation atomic with respect to arena offset and
   registry publication, or declare the entire OPEN upload failed after the
   first error.
5. Prefer staging the new program until commit so a failed upload leaves the
   previous committed program available. If memory forbids two arenas, document
   that OPEN unloads the old program and requires reset/re-upload after failure.

**Acceptance checks:**

- Incomplete programs cannot RUN, STEP, or enter block mode.
- Failed record construction does not leave a usable partial registry entry.
- Commit failure reports the missing/invalid ID and leaves a defined state.
- Python builder emits the commit command.

```powershell
rg -n "VM_LOAD_|vm_loader_|HEADER_packet_vm_|vm_exec_control" components/VM components/codecs Python/PythonRunIT/vm
idf.py build
```

### 8. Coordinate system actions with VM lifecycle

**Problem:** default freeze/resume/suspend/reset actions operate on devices only.
VM pause/reset operates on the VM only. A running program can continue computing
and queueing work while devices are frozen, and device resume can occur before
the program is in the intended state.

**Fix in:**

- `../system/sys_actions/sys_actions.c`
- `../system/sys_actions/SYS_ACTIONS.MD`
- `core/exec/vm_exec.c` and `.h`
- integration tests under `../runit/selftest/`

**Required behavior:**

Define and test one ordered contract for each default action. Suggested order:

| Action | Ordered behavior |
|---|---|
| Freeze | quiesce VM, then freeze device outputs |
| Resume | validate no latched critical fault, sync/resume devices, then resume VM |
| Suspend | quiesce VM, then suspend devices |
| Reset | quiesce VM, reset devices and transient VM state, preserve or unload the program as explicitly specified |
| Hard reset | quiesce VM, uninstall devices, unload VM, then reboot or remain stopped as specified |

Action replay must also become a real transaction: validate stored frame
boundaries, serialize concurrent invocations, and decide whether live RX is
queued or rejected during replay.

**Acceptance checks:**

- No VM block dispatch overlaps device freeze/suspend/reset.
- Resume cannot bypass a latched critical fault.
- Corrupt/truncated stored actions return a structured error.
- Two concurrent action invocations cannot interleave.

```powershell
rg -n "static_fn_freeze|static_fn_resume|static_fn_suspend|static_fn_reset|sys_actions_invoke" components/system/sys_actions
idf.py build
```

## Priority 2 — telemetry, BLE, and other streams

### 9. Correct telemetry identity and dynamic-schema reporting

**Problem:** arena object IDs and dynamic object IDs share the same numeric
range. Subscription deduplication and wire records carry only the number, so a
dynamic child can collide with an arena object. Dynamic object creation,
replacement, and removal have no schema message.

**Fix in:**

- `core/sub/vm_sub.c` and `.h`
- `core/obj/vm_obj_dyn.c` and `.h`
- `../codecs/` telemetry encoder/decoder definitions
- `../Python/PythonRunIT/vm/` and UI decoder
- `../runit/selftest/stage_sub.c`

**Required behavior:**

1. Give every transmitted object identity a namespace and generation, or use a
   single non-colliding ID allocator for arena and dynamic objects.
2. Emit schema before values for a newly visible dynamic tree.
3. Emit replacement/removal when a pointer links to a different dynamic tree.
4. Deduplicate using the complete wire identity, not a bare `uint16_t`.
5. Document client cache invalidation across program reset/reload.

**Acceptance checks:**

- Arena ID 0 and dynamic slot 0 can both appear in one frame unambiguously.
- A clone schema change is decoded without stale client metadata.
- Program reset invalidates all previous dynamic identities.

```powershell
rg -n "get_obj_id|s_emitted|vm_obj_dyn_get_id|frame_append_obj" components/VM/core/sub components/VM/core/obj
idf.py build
```

### 10. Add loss-aware snapshots and correct frame chunking

**Problem:** telemetry drops sender errors, clears update flags afterward, sends
no initial snapshot, and truncates oversized records to one frame while leaving
`start_idx` at zero. Pointer records can be truncated at an odd byte.

**Fix in:**

- `core/sub/vm_sub.c` and `.h`
- `core/exec/vm_exec.c` if acknowledgement changes update clearing
- production sender in `../runit/runit.c`
- `../runit/selftest/stage_sub.c`

**Required behavior:**

1. Subscription sends an initial snapshot or exposes an explicit snapshot flag.
2. Split large objects on element boundaries. Set `start_idx` for every chunk.
3. Never truncate silently. Define the maximum object/record size if chunking is
   intentionally unsupported.
4. Propagate sender failure to subscription state. Retain dirty state or maintain
   a bounded retry/coalescing queue.
5. Define congestion policy: latest-value coalescing is suitable for telemetry;
   reliable ordered delivery should use a different stream.
6. Protect subscription replacement and scanning with an atomic snapshot or lock.

**Acceptance checks:**

- Objects larger than 503 bytes reconstruct exactly on the client.
- Multi-byte elements are never split.
- A failed send is retried/coalesced and is observable.
- Subscribing to an unchanged object still returns its current value.

```powershell
rg -n "VM_SUB_MAX_FRAME_LEN|frame_flush|frame_append_obj|s_sender|vm_sub_subscribe" components/VM/core/sub
idf.py build
```

### 11. Publish VM status and command completion

**Problem:** the status characteristic is created, but no production code sends
`vm_exec_status()`. Remote control acknowledges packet acceptance only through
the absence of an error; clients cannot reliably observe completion or faults.

**Fix in:**

- `../runit/runit.c`
- `../runit/runit_board_cfg.h`
- `core/exec/vm_exec.c` and `.h`
- `../codecs/encoders/` for a status packet
- Python client/UI decoder

**Required behavior:**

1. Define a versioned status packet containing loader state, run mode,
   `scan_active`, `waiting`, next/current block, pass counter, last-pass time,
   program generation, and latched fault state.
2. Publish on state changes and command completion; optionally rate-limit
   periodic metrics.
3. Include a client command/transaction ID if multiple commands can be pending.
4. Keep status reporting independent of telemetry subscriptions.

**Acceptance checks:**

- Client distinguishes command accepted, executing, completed, and failed.
- Reset/reload changes program generation.
- Status remains useful with zero VM object subscriptions.

```powershell
rg -n "SYS_BLE_CHT_RUNIT_STATUS|PACKET_HEADER_STATUS|vm_exec_status" components Python
idf.py build
```

### 12. Make transport integration multi-source and multi-sink ready

**Current state:** inbound transport abstraction is usable. A new source can
register `sys_interface_rx_dequeue_f` with
`sys_interface_register_rx_source()`. The source must deliver complete frames.
Outbound VM telemetry has only one global sender callback.

**Fix in:**

- `../system/sys_interface/sys_interface.c` and public header
- `core/sub/vm_sub.c` and `.h`
- new transport adapter component(s)
- integration tests

**Required behavior:**

1. Add unregister/lifecycle handling for sources that can disappear.
2. Preserve source/session identity through decode when replies are expected.
3. Replace the single telemetry sender with either a registered sink list or a
   router that chooses sinks per subscription/session.
4. Keep byte-stream framing in each adapter; `sys_interface` should continue to
   receive complete frames.
5. Define per-source limits, backpressure, and disconnect behavior.

**Acceptance checks:**

- BLE and a mock second transport can receive commands concurrently.
- Replies return to the correct session or explicitly broadcast by contract.
- Removing a source cannot leave the receiver task dereferencing stale context.

```powershell
rg -n "register_rx_source|bind_ble_rx|s_rx_sources|vm_sub_set_sender" components/system components/VM components/runit
idf.py build
```

## Priority 3 — standalone and non-critical completion

### 13. Implement VM retentive-object persistence

**Problem:** the object header exposes `retentive`, and pointer objects are
correctly rejected, but the VM has no save/restore implementation.

**Fix in:**

- new `core/retention/` module or a clearly owned system persistence component
- `core/loader/vm_loader.c`
- `core/obj/vm_obj_build.c`
- Python program metadata if stable retention keys are compiler-generated
- tests and `VM.MD`

**Required behavior:**

1. Use a stable key containing program identity and object identity/tag; registry
   ID alone is unsafe across different uploaded programs.
2. Restore only after schema/type/size validation.
3. Define write timing and wear control (explicit commit, dirty batching, or
   shutdown checkpoint).
4. Treat missing or incompatible retained data as a normal cold start.

### 14. Turn the watchdog into an explicit recovery policy

**Problem:** the 20 ms block watchdog emits an error but cannot interrupt a
stuck C block. A program lock can then wait indefinitely.

**Fix in:**

- `core/exec/vm_exec.c`
- system watchdog/reset integration
- status/fault reporting

**Required behavior:**

1. Keep the timer callback minimal and latch the offending block/generation.
2. Define whether the response is diagnostic only, task restart, device freeze,
   or full MCU reset. Since arbitrary C cannot be safely preempted and resumed,
   a controlled system reset is usually safer than deleting the VM task.
3. Add a whole-scan budget separately from the per-block watchdog.
4. Persist/report the reset reason on next boot.

### 15. Reduce misleading and recursive telemetry logging

**Problem:** the production telemetry sender logs routine data at warning level,
interprets every four-byte payload as a float, and produces logs that are
themselves mirrored over BLE.

**Fix in:**

- `../runit/runit.c`
- `core/sub/vm_sub.c`
- `../sys_errors/sys_error.c`

**Required behavior:**

1. Remove payload interpretation from the transport sender; it lacks object type
   metadata.
2. Move routine packet dumps behind a dedicated debug flag and use debug level.
3. Keep production logs to state changes, loss counters, and actionable faults.
4. Correct the `ignere errors generated during sendings` comment while touching
   the logging code.

### 16. Measure real target memory and timing limits

This is validation rather than an architecture change. Test the documented
approximately 300-block/~77 KiB program on target, including contiguous internal
DRAM availability after all devices and BLE buffers initialize.

**Measure:**

- largest internal-DRAM block before and after device/BLE initialization;
- loader arena requested/used bytes;
- worst and percentile scan duration with realistic device traffic;
- telemetry and error bursts while BLE is congested;
- stack high-water marks for VM, interface, error, callback, and action tasks.

Use `RUNIT_ENABLE_VM_BENCH` only in a development profile and record board,
firmware revision, compiler settings, and program fixture with the results.

## Completion gate

The VM is ready for production integration when all priority 0 and priority 1
items are complete and the following end-to-end scenario passes on hardware:

1. Boot with production configuration and installed devices.
2. Upload, validate, and commit a program over BLE.
3. Subscribe and receive an initial snapshot.
4. Run it, override scalar input, and observe chunk-safe telemetry.
5. Inject a warning device fault and verify the configured action without stop.
6. Inject a critical device fault and verify VM quiescence, safe device state,
   latched status, and coherent serial/BLE error reporting.
7. Acknowledge/recover according to policy and resume without reboot if allowed.
8. Interrupt a new upload and verify that the prior committed program remains
   valid, or that the documented empty/fault state is enforced.

Priority 2 is required before claiming general multi-stream or reliable remote
observability support. Priority 3 items can be scheduled independently once the
production blockers and lifecycle contracts are resolved.
