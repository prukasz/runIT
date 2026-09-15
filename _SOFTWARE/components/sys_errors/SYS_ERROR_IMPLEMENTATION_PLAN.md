# System error policy — implementation plan

Status: proposed implementation specification; no firmware changes made. Sections 13–21 provide the concrete implementation contracts and supersede earlier placeholders where they specify an exact choice.

Rework requirement: replace the error subsystem and its firmware/host contracts in one coordinated release. There are no compatibility APIs, alternate protocol readers, automatic handler substitutions, or staged dual implementations. This requirement supersedes workbook answers describing chained classifiers. Missing required configuration rejects installation/startup; missing runtime invariants produce an explicit policy-configuration fault. Mandatory critical handling and reserved emergency storage remain part of the primary design.

Source: [QA workbook](SYS_ERROR_QA_WORKBOOK.md), including its final comments. Prepared 2026-09-15 against the current source tree. Names introduced below are proposed APIs, not existing APIs. Complete the phases in order; do not enable the new synchronous policy until its ownership and concurrency requirements are implemented.

## 1. Target behavior and decisions

- Safety reactions execute synchronously in task context, independently of logging, BLE delivery, and VM callback subscriptions.
- Device operations classify and handle faults after releasing driver locks. Callers retain the error and add context. The outer reporting boundary publishes the completed chain once.
- ISR faults latch/inhibit immediately using an ISR-safe API; a notification wakes the dedicated safety-control task to perform the response. No error-handler task or general error queue remains.
- Each device has an instance role, health, isolation status, configurable classification rules, and one scoped action binding per severity.
- VM code can inspect selected faults as error objects. Critical faults stop ordinary VM execution; their records remain inspectable while stopped.
- Serial traces remain synchronous. BLE enqueue is nonblocking; full queues drop telemetry and increment counters without suppressing safety.

### Explicit resolutions of ambiguous or conflicting answers

These are recommendations from the review, made explicit for future implementation. They are not claims that the workbook selected them.

| Workbook item | Implementation decision |
| --- | --- |
| Q5: static boolean guard | Use per-execution response context plus synchronized incident state. A global boolean would drop unrelated concurrent faults. |
| Q13: extend state with health | Use a composite runtime state containing lifecycle, health, and isolation. Preserve lifecycle semantics; do not append health values to the ordered lifecycle enum. |
| Q14: first-success healing | A successful hardware operation/probe clears device health. It does not clear the global critical latch, resume a suspended device, or re-enable isolated outputs. |
| Q15: freeze before VM latch | Change to latch/inhibit first, coordinate in-flight operations, then freeze and invoke the action. Never wait for the current VM pass to stop itself. |
| Q18: board remains safe after response failure | Keep execution/output access inhibited; report response failure. Do not claim physical safety when an adapter freeze failed. |
| Q20: per-block policy | Default script errors to WARNING; keep severity separate from existing block STOP/CONTINUE behavior. |
| Q9: adapter plus central classifier | Replace the classifier chain with one complete per-instance policy table. Adapters identify the cause/category; the policy table determines severity. |
| Q22/Q23: future power handler | Require an explicitly registered power handler before enabling power producers. Handler failure is a response failure; no substitute handler is selected. Future load shedding is outside this change. |
| Q25: intentional disconnect | Use a one-shot, connection-session-scoped flag. Malformed frames are NOTICE unless their underlying failure independently matches a mandatory critical rule. |
| Q27 versus Q29 | Replace three bare action IDs with three explicit scope/ID pairs. |
| Bottom comment: no memory = warning | Real allocator/VM arena exhaustion stays CRITICAL per Q21/Q26. Buffer-full notification/telemetry conditions get distinct tags and do not masquerade as allocation failure. |

## 2. Current implementation gaps

- `sys_error_handler.c` queues raw `err_h` pointers and only publishes them; it does not implement the proposed central safety path.
- `sys_device_report_error()` is opt-in and currently lets the adapter callback bypass central policy. `SYS_DEV_DISPATCH` returns failures without invoking it.
- `sys_device_classify_error()` uses the root tag only, with no operation, role, or instance-rule input.
- `sys_device_freeze_all()` shares a sweep that returns at its first failure. Adapter freezing does not itself establish a universal dispatch barrier.
- `runit_device_error_policy()` uses VM fault latching to suppress repeat global responses and searches both action scopes. Replace it with the new incident coordinator and exact scoped bindings.
- `vm_exec_stop()` returns `ERR_VM_EXEC_SELF_BARRIER` when called by the active pass. Watchdog handling cannot depend on a hung pass returning.
- Error storage is a 2048-byte overwrite ring. A pointer can become invalid while policy, callback delivery, or another task allocates errors. `SE_error_root()` currently has no traversal bound.
- `vm_event.c` queues copied callback envelopes and keeps a one-cycle snapshot. It has no retained chain ownership or persistent critical-error object.
- `sys_callback_trigger()` reports queue saturation as `ERR_BASE_NO_MEM`; with the proposed global memory policy this would incorrectly escalate notification congestion.

## 3. Error lifetime and incident model

### 3.1 Replace overwrite-based ownership before adding callbacks

Use a bounded, statically allocated node/payload pool with explicit reference ownership. Nodes must never be overwritten while referenced. Keep the public `err_h` concept where practical; remove the assumption that handles need no release.

Required APIs and rules:

- `SE_retain(err)` / `SE_release(err)` manage references; releasing the final reference releases its owned cause reference.
- A newly created error owns one reference. Returning it transfers ownership to the caller.
- Wrapping consumes the caller's cause reference and returns one owned outer handle. Add an explicitly named borrowed-wrap helper only if needed.
- A reporting boundary borrows the chain while processing it; the boundary macro then releases its owned reference. Audit manual `SE_push_to_handler()` calls individually, especially callers that subsequently return the same error.
- Store incident metadata in a separate bounded pool shared by all wrappers: boot-session/sequence identity, source, originating device and operation, effective severity, configuration revision, policy state, publication state, response outcome, and parent incident for response failures.
- A genuine new failure creates a new incident. Wrapping an existing cause shares its incident. Do not deduplicate by raw pointer or by tag/device alone.
- Allocation failure must still latch a minimal critical record through reserved storage without allocating, logging recursively, or dereferencing NULL. Existing construction macros write payloads directly, so all allocation/macro failure paths must be audited.
- Bound every chain traversal, including root lookup, classification, logging, encoding, and release. Detect malformed links and expose incomplete-chain status.
- Size pools through Kconfig/static constants and document worst-case RAM. Reserve capacity for critical metadata independently of normal telemetry/event occupancy.

Audit all constructors, wrappers, returned handles, discarded handles, stored handles, and error-returning sink calls across `components/`. Existing selftests must also release handles. Pool exhaustion and reuse tests are a prerequisite for enabling the new policy.

### 3.2 Policy and publication are separate operations

Proposed interface:

```c
// Borrows err; may apply safety, never publishes the ordinary completed chain.
void SE_handle_incident(err_h err, const se_origin_context_t *context);
// Borrows err; handles an unhandled incident, then publishes at most once.
void SE_report(err_h err, const se_origin_context_t *context);
```

- Delete `SE_push_to_handler()` and replace every call with `SE_report()` plus explicit ownership handling. Do not retain an alias or wrapper.
- `SE_ORIGIN_CALL` reports/releases a returned error. `BLOCK_CALL` delegates to `vm_block_report_error()`, which wraps block identity, sets block-fault state, reports, and releases.
- `SE_REPORT_DEV_ERR` creates, handles, publishes, and releases a standalone device event. Returned failures must use the propagation path instead.
- `SE_EMIT_ERR` remains a terminal reporting boundary for asynchronous task-context sources such as the watchdog.
- No serial/BLE/user callback execution before the mandatory safety response. Encode from stable owned storage after policy completes.
- Repeated boundary calls on the same live incident do not rerun actions or republish. Separate occurrences still increment counters and receive different identities.
- Multiple device wrappers around one dependency failure preserve the originating device. Dependent devices may become unavailable, but must not repeat the originating action. Explicitly independent secondary failures get new incidents.

Example:

```text
VM block -> sys_io write -> device adapter -> I2C failure
  driver releases lock; device adds device/WRITE context
  classify and apply incident policy once
  return error -> block adds block identity
  report complete block -> device -> I2C chain once
  selected VM notification retains the completed chain
```

If a critical fault never reaches its outer boundary, its reserved critical record still exposes the original cause and response outcome. It must not pretend to contain block context that was never added. Watchdog detection produces a distinct incident for the hung execution.

## 4. Classification and device configuration

### 4.1 Types

- Roles: `SYSTEM_CRITICAL`, `IMPORTANT`, `USER`, `TEST`. Require every installation to supply an explicit validated role and complete policy configuration; implicit zero-initialization is not configuration.
- Define severity values CRITICAL=0, WARNING=1, NOTICE=2. These are the only classifier results; reject all other values. “Info” means NOTICE.
- Operations: ANY, READ, WRITE, CONFIGURE, INSTALL, RESET, FREEZE, POWER, PROBE. Store them explicitly; do not infer reads/writes from function names at runtime.
- Categories: COMMUNICATION, DEVICE_MISSING, POWER, INVALID_ARGUMENT, UNSUPPORTED, MEMORY, INTERNAL_STATE, SCRIPT, TRANSPORT, OTHER. Maintain a tag-to-category table; retain original tags.
- Rule: operation selector, match-kind (exact tag/category), match value, severity. Use a fixed-capacity per-device rule table; reject duplicates and invalid values.
- Action binding: `{scope, id}` per severity. Scope values STATIC=0, DYNAMIC=1, NONE=255. NONE requires id=0 and explicitly disables the optional action. Every severity must have an explicit binding. Mandatory safety is independent of these bindings.
- Device runtime state: lifecycle enum, health enum (OK/DEGRADED/FAULTED), isolated flag, last incident identity, and saturating fault count.

### 4.2 Single compiled classification policy

1. Mandatory critical tags are explicit system policy: real allocation exhaustion, VM arena exhaustion, VM block watchdog, confirmed wedged bus, unexpected established-session disconnect and severe rail faults. They cannot be downgraded.
2. Every device installation supplies a complete category matrix for every concrete operation, three action bindings, and its exact-tag rules. Board source may construct that matrix from the role profiles below, but the installed object contains concrete values for every cell, not links to inherited policy.
3. At configuration commit, expand ANY selectors and compile rule overrides into that single policy. Resolve specificity at compile time: exact operation+tag, ANY+tag, exact operation+category, ANY+category; reject conflicting rules of equal specificity. Runtime performs one lookup for the actual operation/tag/category.
4. Adapters report precise tags/categories and operation context; remove severity-classification hooks. Unknown tags must explicitly identify OTHER. A missing/invalid category or incomplete policy is `ERR_POLICY_CONFIG_INVALID`, a mandatory critical system fault, not an invitation to try another classifier.

| Category | SYSTEM_CRITICAL | IMPORTANT | USER | TEST |
| --- | --- | --- | --- | --- |
| Communication / missing physical device | CRITICAL | WARNING | WARNING | NOTICE |
| Invalid argument / unsupported capability | NOTICE | NOTICE | NOTICE | NOTICE |
| Power / memory / internal state | CRITICAL | CRITICAL | CRITICAL | CRITICAL |
| Script | WARNING | WARNING | WARNING | WARNING |
| Transport | NOTICE | NOTICE | NOTICE | NOTICE |
| Other | CRITICAL | WARNING | WARNING | NOTICE |

Mandatory rules apply to all roles. Generic invalid state requires explicit tag/context mapping rather than automatically equating all API misuse with system failure. Add an explicit confirmed-bus-wedged tag; an ordinary I2C timeout is not sufficient evidence by itself.

Severity reactions: CRITICAL inhibits VM/device access and freezes globally; WARNING isolates/freezes the affected physical device and leaves other devices/VM running; NOTICE records/publishes only. Argument validation failures do not degrade physical health. Script WARNING uses block behavior rather than device isolation. Freeze failure records response diagnostics without re-entering the ordinary classifier. The role profiles are explicit board configuration constructors, not runtime inheritance.

### 4.3 Configuration installation and mutation

- Add role and complete fault-policy configuration to every typed `d_*_cfg_t`; pass policy explicitly to `sys_device_install_cfg()` through `SYS_DEVICE_CREATE`. Do not cast opaque config bytes to guess their layout.
- Initialize role/policy before invoking adapter install. Preserve role/source metadata in a failed-install incident after registry removal.
- Delete `use_error_handler`, `generate_error_callback`, bare `actions[]`, and adapter `error_handler` implementations. Implement classification and VM subscription as separate mechanisms.
- Configuration setters validate a complete replacement, then commit atomically with a revision number. Classification captures one coherent revision; an in-flight response does not change when configuration changes.
- Update dispatch macros and hand-written IO/power/lifecycle paths. Evaluate call arguments once; attach explicit operation, process failure after locks unwind, and record successful hardware transactions for healing.
- Nested cross-device adapter calls must propagate until all relevant transaction locks are released. Carry context through wrappers; never freeze recursively while holding a bus/device lock.
- `freeze` and isolation are distinct: deny ordinary writes to isolated devices even if an adapter's freeze implementation merely sets a flag. Provide a narrow policy-internal bypass for teardown and registered safety actions, never a general VM bypass.
- Add a recovery probe API allowed for faulted/isolated devices. Success clears health; explicit recovery clears isolation only after checking the global latch and lifecycle. Failed probes update diagnostics without continuously replaying the same isolation action.

## 5. Safety coordinator, concurrency, and ISR handling

Implement application coordination in `runit`, registered through transport/VM-independent hooks in `sys_errors` and `sys_device`.

Critical response sequence:

1. Atomically record the incident, latch the VM fault, and inhibit new VM blocks and ordinary device operations.
2. Coordinate already-admitted device operations using a dispatch barrier/in-flight accounting. Recheck inhibition before hardware mutation. Do not hold coordinator locks across driver calls.
3. Quiesce where possible with bounded waits. Inside the active VM pass, request stop and unwind; never call its blocking stop barrier. An unresponsive pass does not prevent the control task from attempting hardware freeze.
4. Attempt every eligible device freeze in dependency-safe teardown order; collect all failures without aborting the sweep.
5. Invoke the bound action exactly once in its declared scope, with the restricted safety context. A dynamic action that requires normal VM execution cannot run after a critical stop: reject that binding at configuration time. Missing bound actions are configuration errors, never another scope lookup.
6. Persist response status and failed-device/stage details; publish the original incident when its reporting boundary is reached.

Concurrency requirements:

- Use short critical sections/atomics for incident policy claims and global response state; no driver calls, blocking waits, action calls, encoding, or logging inside them.
- A thread encountering the same in-progress incident must not rerun it or deadlock waiting on itself. Coordinate publication after its response result is stable.
- Different simultaneous incidents retain separate records, counts, and required per-incident actions. A global freeze already in progress can satisfy their shared freeze requirement; serialize actions safely without dropping them.
- Response-generated failures use a task-local/nested response context and become `ERR_DEV_FAULT_RESPONSE_FAILED` records linked to the original incident. They cannot re-enter normal response policy or callback actions. Independent faults on other tasks are not suppressed.
- Time-bound lock acquisition and bus operations used during freeze. Record inability to quiesce/freeze; keep inhibition latched. Software cannot guarantee physical freeze when hardware or a required lock is unresponsive.
- Replace global suppression as a safety mechanism: `SE_suspend()` may suppress diagnostics for rollback, but must not suppress independent mandatory critical faults. Make rollback suppression scoped to the calling execution context.

ISR path:

- Add a dedicated `SE_report_from_isr()` accepting a small fixed scalar fault record, not a transient `err_h` chain.
- Add a genuinely ISR-safe VM fault latch/request-stop function; do not assume the existing task critical-section API is safe from ISR.
- Store a bounded ISR mailbox record and set a notification bit. An overflow must preserve critical pending state and a dropped-detail count.
- Add the dedicated safety-control task, independent of the VM pass and general callback task, solely for deferred ISR/watchdog/blocked-context work. This is an explicit exception to task removal, not a replacement logging queue.
- Document startup behavior before that task is ready: latch/inhibit immediately and drain pending work during initialization. Enable interrupt producers only after hooks/storage are ready.
- Keep the watchdog detector bounded; request immediate inhibition and hand blocking freeze work to the control task so timer service is not stalled.

## 6. VM error callbacks and objects

- Add `CALLBACK_ERROR` and an error-event union member containing incident identity and a retained-chain handle. Keep `SYS_CB_ROUTE_VM` as the destination.
- Add explicit retain/release helpers for callback envelopes. Queue success transfers/retains ownership by documented API convention; enqueue failure releases it. Dispatch fan-out retains per destination. VM queue drain, cycle replacement, reset, program unload, and unregistered routes release their references.
- Error envelopes have zero generic static/dynamic action IDs. Safety actions were already invoked by policy; callback routing must not invoke them again.
- Add fixed-capacity subscriptions matching source kind, device selector, severity mask, and optional operation/tag/category. Fields within one filter are ANDed; matching filters are ORed. Deliver at most one VM notification per incident. Default subscriptions are empty; global critical status is always retained independently.
- Publish noncritical notifications after the full chain has been wrapped at the boundary. Drain them at a VM execution boundary, never execute VM code on a driver/ISR/callback thread.
- Add a read-only VM error view/object exposing identity, source, device, operation, severity, block context where available, root tag/owner, ordered node payloads, response status, and completeness flags. Implement bounded accessors plus a block/API for selecting/reading an error event using the existing object/accessor system.
- Use preallocated error-object slots. Ordinary VM allocation failure cannot prevent critical record capture. Explicit event capture retains its chain until handle release or error-object reset; copying a U32 token does not retain it. Cycle-scoped views expire at the next drain. Section 15.4 defines the exact lifetime contract.
- Keep a separately retained first-critical record with occurrence count and response status, inspectable while VM execution is stopped. Acknowledge releases/clears it only through the stopped/quiescent recovery API. Acknowledge does not automatically resume VM or hardware.
- Ordinary VM handlers do not run after a critical stop. A future restricted recovery interpreter is outside scope; expose the record to host commands and explicit post-acknowledgment recovery logic.
- Callback queue/pool saturation increments diagnostics and drops notifications, never drops safety. Do not recursively generate an error callback for failure to enqueue an error callback.
- Preserve `g_vm_block_fault` and existing ENO/STOP/CONTINUE behavior. Add a separate block severity setting, default WARNING; do not reinterpret the existing `on_error` byte as severity. Block settings cannot downgrade device incidents already handled or mandatory system faults.

## 7. Non-device sources

| Source | Required change |
| --- | --- |
| VM watchdog | Mandatory critical incident with block identity; detector must work when the VM pass is hung. |
| Script arithmetic/type errors | Block severity policy, default WARNING, existing control-flow behavior retained. |
| VM arena / allocator exhaustion | Reserved critical recording and safe-state path with no dependency on fresh allocations. |
| Power budget / rail faults | Require one registered power handler with a concrete result contract before enabling producers. Severe rail faults always invoke mandatory critical handling; budget actions follow explicit power configuration. Handler failure records `ERR_DEV_FAULT_RESPONSE_FAILED`; missing registration blocks startup. |
| Confirmed wedged I2C bus | New explicit tag and immediate critical handling; no recovery-clock implementation in this scope. |
| BLE disconnect | Capture active session and one-shot intentional-offline flag before clearing connection state; intentional is NOTICE, unexpected is CRITICAL. Never-connected boot is not a disconnect fault. |
| Malformed interface frames | NOTICE and receiver continues; actual memory exhaustion follows the mandatory critical rule. |

Intentional-offline API: set/cancel for the current connected session only, acknowledge application of the flag before the client disconnects, consume on disconnect, clear on reconnect/reset. No indefinite exemption across sessions. Route all mandatory loss handling independently of optional BLE callback subscriptions and outside the BLE mutex.

## 8. Wire protocol and host changes

### Device policy packet `0x1A`

Define a single complete 89-byte device policy payload:

```text
u8 version (=2)
u8 device_id
u8 role
u8 critical_scope; u8 critical_id
u8 warning_scope;  u8 warning_id
u8 notice_scope;   u8 notice_id
u8 severity_matrix[8][10]; // concrete operations 1..8, categories 0..9
```

Decoder accepts only the new exact length/version. Apply the validated update with one manager API call; all other layouts are invalid input.

Additional logical commands (concrete proposed IDs and payloads are in section 17):

- Replace/read device classification rules: version, device ID, count, then `{u8 operation, u8 match_kind, u16 match_value, u8 severity}` entries. Clear with count=0; reject oversized tables atomically.
- Replace/read VM error subscriptions: version, count, fixed records containing source selector, explicit any-device flag/device ID, severity mask, operation selector, and match-kind/value. Do not steal a valid device ID as a wildcard.
- Set/cancel intentional-offline flag for the active session; return applied session identity.
- Read device state/policy revision, critical fault record, and drop counters; explicit device recovery and existing VM fault acknowledgment remain separate commands.
- Configure block fault severity separately, or bump the loader format with updated length/version validation; preserve existing STOP/CONTINUE encoding.

Copy the exact IDs, field widths, limits, request/response bytes, and golden vectors from sections 17–18 into `CODECS.MD` during implementation; recheck their availability against the implementation branch.

### Error telemetry

- Bump the error encoder format to v2. Keep ordered node records (tag, owner, payload length, original payload) so the host reconstructs the actual chain, not just its root.
- Add the 44-byte envelope defined in section 18, containing boot/session + incident sequence, source kind, explicit has-device flag/device ID, operation, effective severity, policy revision, response outcome, and completeness flags. Use that same layout on both ends.
- Define the new tag/owner schema and payload ABI in one shared specification consumed by firmware and host tooling. Regenerate both ends together; existing numeric assignments are not a compatibility constraint.
- Report depth limit, storage exhaustion, and packet truncation explicitly. The host must never present a truncated record as a complete digital twin.
- Verify the BLE sink copies bytes before returning and uses zero wait on every enqueue/lock path. Update transport APIs if the current `sys_ble_char_send(..., true)` does not meet this requirement.
- Implement real drop counters for storage, ISR details, callbacks, VM events, and BLE packets. Transport failure must not publish recursively through the failing transport.
- Replace the serializers, error decoders, device configuration UI, VM object tooling, and tests under `Python/PythonRunIT/` listed in section 19. Firmware and deployed clients must use the same new schema; do not include conversion paths.

## 9. File-level work map

| Files / area | Changes |
| --- | --- |
| `components/sys_errors/include/sys_error.h`, `sys_error.c` | Ownership, bounded pool, incident metadata, safe traversal, macros, counters, scoped suppression. |
| `components/sys_errors/sys_error_handler.c` | Remove `s_err_queue` and consumer task; implement synchronous report/encode/trace, preserve idempotent initialization. |
| New `components/sys_errors/include/sys_error_policy.h`, `sys_error_policy.c` | Context, categories, central non-device rules, handle/report coordination and hook contracts; no direct VM dependency. |
| `components/sys_errors/include/sys_error_config.h` | Verify/implement nonblocking transport adapter; refresh API comments. |
| `components/system/sys_device/include/sys_device.h`, `sys_device.c` | Instance policy, role/rules/hooks, composite state, dispatch barriers, operation tracking, recovery, full freeze sweep. |
| `components/system/sys_device/include/sys_error_dev.h` and domain error headers | New precise tags/payloads, response outcome detail, logger registrations. |
| `components/system/sys_io/`, `sys_power/`, `sys_i2c/` | Instrument dispatch and manual paths; distinguish queue/full, API misuse, allocation failure, and confirmed bus wedge; lock audit. |
| `components/devices/device_*/` | Complete typed policy config, precise category/tag context, lock-safe returns, bounded freeze behavior, deletion of callback/classifier hooks. |
| `components/runit/runit.c`, `runit_board_devices.h` | Coordinator, required boot registrations, explicit board policies/action bindings, single boot failure path. |
| New `components/runit/runit_fault_control.c/.h` | Required dedicated safety control for ISR/watchdog and blocked-context work. |
| `components/system/callbacks/include/sys_callbacks.h`, `sys_callbacks.c` | Error event, ownership-aware fan-out and queue handling, distinct queue-full outcome. |
| `components/VM/core/exec/vm_exec.c/.h`, `vm_event.c/.h` | ISR/task inhibition, self-pass handling, retained error events, persistent critical record and safe reset. |
| `components/VM/core/block/vm_block.h`, `vm_block_build.c`, `vm_block_run.c`, `core/errors/sys_error_vm.h` | Unified block reporting, full chain wrapping, separate severity config, macro ownership. |
| `components/VM/core/obj/`, accessor/build code, `components/VM/blocks/` | Read-only error view, event selection/read API/block, retained object lifetime and serialization. |
| `components/system/ble/sys_ble_stack.c`, `sys_ble.c`, `sys_ble_priv.h`, `include/sys_ble.h` | Session-scoped offline intent, disconnect policy hook, nonblocking publication audit. |
| `components/codecs/decoders/dec_sys_contracts.h`, `dec_sys_device_install.h`, `dec_vm_loader.h` | Versioned policies, config roles, block severity and new command dispatch validation. |
| `components/codecs/encoders/enc_sys_errors.h` | v2 envelope, stable full-chain encoding, explicit completeness. |
| Component `CMakeLists.txt`, Kconfig owners | Register new sources/includes and static capacity settings; prevent circular component dependencies. |
| `Python/`, documentation | New protocol implementation, configuration/readback, error objects and complete-chain rendering. |

## 10. Implementation phases and acceptance gates

### Phase 1 — contract and ownership foundation

- [ ] Finalize tag/category table, source/operation IDs, capacities, additional packet IDs and v2 byte layouts.
- [ ] Add ownership/pool/incident primitives and rewrite all error-handle users to the new contracts.
- [ ] Add safe traversal, reserved critical storage, queue-full tags and counters.
- [ ] Gate: retain/release, concurrent allocations, full pool, wrapper lifetime and all existing error tests pass with no leaked references.

### Phase 2 — device model and classification

- [ ] Add roles, state, rules, action binding and coherent config updates.
- [ ] Replace all device configs, creation paths and board assignments with complete explicit policies.
- [ ] Instrument all returned and standalone fault paths with explicit context.
- [ ] Gate: table-driven classification and role/operation override tests pass, including nested dependencies and failed install after registry removal.

### Phase 3 — safety execution

- [ ] Implement coordinator, inhibition/admission barriers, full freeze sweep, response-failure isolation, ISR/control-task path and bounded watchdog integration.
- [ ] Wire mandatory non-device rules and enforce required startup registration.
- [ ] Gate: device→block causes exactly one policy action; concurrent independent faults survive; no self-barrier deadlock; later devices freeze after an earlier failure.

### Phase 4 — synchronous publication and VM integration

- [ ] Delete the error consumer task/queue and implement every reporting boundary directly with the new synchronous API.
- [ ] Add callback ownership/filtering, VM error objects and retained critical record.
- [ ] Add separate per-block severity while preserving current control flow.
- [ ] Gate: completed block chains reach telemetry/VM intact; reset/unload/overflow paths release references; stopped VM faults remain inspectable.

### Phase 5 — protocols, transport and clients

- [ ] Implement policy/rule/subscription/state/recovery/offline commands and error format v2 on firmware and host.
- [ ] Enforce zero-wait BLE publication; add truthful counters.
- [ ] Gate: new-schema golden byte vectors pass on both ends; all other versions/layouts reject; partial updates cannot occur; malformed frames keep receiver alive.

### Phase 6 — integration validation and documentation

- [ ] Extend selftests listed below, register added stages, build firmware using the repository's ESP-IDF environment, and run the complete configured selftest suite on the target.
- [ ] Run host codec/unit tests and hardware fault-injection checks; report which results require hardware and were not run.
- [ ] Update `SYS_ERRORS.MD`, `SYS_DEVICE.MD`, `VM.MD`, `CODECS.MD`, callback/ BLE documentation and the workbook with resolved decisions and implemented semantics.
- [ ] Audit stale queue/task comments, booleans, unscoped action lookup, unowned error handles, uninstrumented dispatch and ignored freeze failures.

## 11. Required validation matrix

Extend `components/runit/selftest/stage_sys_errors.c`, `stage_device_fault.c`, `stage_exec.c`, `stage_contracts.c`, `stage_block_support.c`, `stage_obj.c`, `stage_actions.c`, and `stage_boot.c`; add callback tests where appropriate.

| Scenario | Required result |
| --- | --- |
| Device write fails inside VM block | One incident/response/action; full block→device→bus chain; fault count increments once. |
| Same incident reaches two boundaries | No duplicate action or ordinary publication; ownership remains valid. |
| Two tasks fault simultaneously | Both incidents recorded; global freeze coordinated; no lost local actions or counters. |
| Same tag/device fails again later | New identity and occurrence; not confused with duplicate propagation. |
| Critical from active VM block | Immediate inhibition, no self-wait; pass unwinds and hardware freeze is attempted. |
| Hung VM / held bus lock | Watchdog still latches; bounded freeze attempts; failed-response status instead of deadlock or false safe-state claim. |
| Complete/incomplete compiled policy | Complete table classifies with one lookup; incomplete config rejects atomically; mandatory critical rules remain enforced. |
| READ warning / WRITE critical rule | Same root tag produces configured operation-specific behavior. |
| USER warning / TEST notice | USER isolated, others continue; TEST logged without isolation by default. |
| Failure in freeze or safety action | Remaining devices attempted; original incident preserved; linked failure with no recursion. |
| ISR fault and ISR mailbox overflow | Latch survives; task performs response; details loss counted; no ISR mutex/I2C/logging. |
| Driver lock held at inner dependency failure | Response postponed until safe unwinding boundary; no recursive lock acquisition. |
| Memory/arena exhaustion | Critical path still works without heap allocation or ordinary error-pool capacity. |
| Callback/TX queue full | Safety unaffected; dedicated drop count; no false allocator fault or callback loop. |
| Callback filters match multiple entries | At most one event, complete chain, original effective severity preserved. |
| VM cycle rollover, queue reset, unload, object deletion | Correct retain/release; no stale view dereference or leaked pool entries. |
| Critical with VM already stopped | Persistent inspectable error; no user callback execution; acknowledgment does not resume outputs. |
| Successful recovery probe | Health clears; isolation/global latch remain until explicit recovery/acknowledgment. |
| Config changes during response | Captured revision used consistently; invalid update changes nothing. |
| Scoped action / missing action / non-executable dynamic action | Exact scope invoked once; invalid bindings rejected; no second scope lookup. |
| Intentional/unexpected disconnect, reconnect, boot offline | One-shot intent works only in its session; unexpected established-session loss critical; boot offline unaffected. |
| Missing/failed power handler | Missing registration prevents producer startup; failure produces a linked response-failure record and preserves original power context. |
| Deep/truncated/malformed chain | Traversal terminates; completeness flags truthful; host reconstructs available nodes safely. |
| New protocol and unsupported version/schema | Only the declared schema is accepted; invalid versions reject; unknown payload records remain bounded and visibly unknown. |
| Boot failure before callbacks/transport ready | Mandatory safety remains available; no dependence on ready BLE or VM event queues. |

## 12. Definition of done

All six phases and their gates are complete; no device fault can bypass mandatory policy through callback settings; no VM wrapper repeats an existing device response; critical records survive notification loss; no retained chain can be overwritten; every eligible freeze is attempted; firmware and host agree on the new protocol; test results distinguish host, target selftest, and physical fault-injection coverage.

Out of scope: automatic bus recovery, new power load-shedding algorithms, automatic VM restart after critical faults, a VM interpreter that runs user recovery code while the normal VM is stopped, and guaranteed hardware emergency cutoff without supporting hardware.

## 13. Exact shared types and API changes

The declarations below define the intended interface, not code to paste without integrating existing includes and forward declarations. Internal C structs are never serialized with `memcpy` unless explicitly designated as wire payloads.

### 13.1 `sys_error_policy.h` (new)

Assign these numeric values once and mirror them in host code:

```c
typedef enum {
  SE_SOURCE_SYSTEM = 0, SE_SOURCE_DEVICE = 1, SE_SOURCE_VM = 2,
  SE_SOURCE_POWER = 3, SE_SOURCE_TRANSPORT = 4
} se_source_e;
typedef enum {
  SE_OP_ANY = 0, SE_OP_READ = 1, SE_OP_WRITE = 2,
  SE_OP_CONFIGURE = 3, SE_OP_INSTALL = 4, SE_OP_RESET = 5,
  SE_OP_FREEZE = 6, SE_OP_POWER = 7, SE_OP_PROBE = 8
} se_operation_e;
typedef enum {
  SE_CATEGORY_COMMUNICATION = 0, SE_CATEGORY_DEVICE_MISSING = 1,
  SE_CATEGORY_POWER = 2, SE_CATEGORY_INVALID_ARGUMENT = 3,
  SE_CATEGORY_UNSUPPORTED = 4, SE_CATEGORY_MEMORY = 5,
  SE_CATEGORY_INTERNAL_STATE = 6, SE_CATEGORY_SCRIPT = 7,
  SE_CATEGORY_TRANSPORT = 8, SE_CATEGORY_OTHER = 9
} se_category_e;
typedef struct {
  uint32_t boot_id;
  uint32_t sequence;
} se_incident_id_t;
typedef struct {
  uint8_t source;
  uint8_t operation;
  bool has_device;
  uint8_t device_id;
  bool has_block;
  uint16_t block_idx;
  uint8_t block_type;
} se_origin_context_t;
typedef enum {
  SE_POLICY_NEW = 0, SE_POLICY_ACTIVE = 1, SE_POLICY_DONE = 2
} se_policy_state_e;
typedef enum {
  SE_RESPONSE_NONE = 0, SE_RESPONSE_OK = 1,
  SE_RESPONSE_PARTIAL = 2, SE_RESPONSE_FAILED = 3
} se_response_e;

void SE_handle_incident(err_h error, const se_origin_context_t *context);
void SE_report(err_h error, const se_origin_context_t *context);
bool SE_incident_info(err_h error, se_incident_info_t *out);
```

`se_incident_info_t` is a copied, read-only value containing the ID, context, effective severity, policy state/revision, response, completeness flags and occurrence count. Never return a mutable pointer into incident storage. Generate `boot_id` at initialization and monotonically allocate nonzero sequence values; rotate the boot/session identity before sequence reuse.

Define match-kind ANY=0, TAG=1, CATEGORY=2. ANY is valid for subscriptions; device rules require TAG or CATEGORY. ANY operation is a selector only: a real operation must provide its actual operation where known.

### 13.2 Device declarations

Add these declarations in `sys_device.h`; define the policy types before device installation/configuration declarations:

```c
typedef enum {
  SYS_DEV_ROLE_SYSTEM_CRITICAL = 0, SYS_DEV_ROLE_IMPORTANT = 1,
  SYS_DEV_ROLE_USER = 2, SYS_DEV_ROLE_TEST = 3
} sys_device_role_e;
typedef struct { uint8_t scope; uint8_t id; } sys_device_action_ref_t;
typedef struct {
  uint8_t operation;
  uint8_t match_kind;
  uint16_t match_value;
  uint8_t severity;
} sys_device_fault_rule_t;
typedef struct {
  uint8_t role;
  sys_device_action_ref_t actions[3];
  uint8_t severity_matrix[8][10];
} sys_device_policy_config_t;
typedef struct {
  sys_device_state_e lifecycle;
  uint8_t health;                 // OK=0, DEGRADED=1, FAULTED=2
  bool isolated;
  uint32_t fault_count;
  se_incident_id_t last_incident;
} sys_device_runtime_state_t;

err_h sys_device_install_cfg(const sys_device_class_t *cls, uint8_t device_id,
                            const sys_device_policy_config_t *policy,
                            const void *cfg, size_t cfg_size);
err_h sys_device_set_error_handling(uint8_t device_id,
                                  const sys_device_policy_config_t *cfg);
err_h sys_device_set_fault_rules(uint8_t device_id,
                               const sys_device_fault_rule_t *rules, size_t count);
err_h sys_device_get_fault_config(uint8_t device_id,
                                sys_device_fault_config_snapshot_t *out);
err_h sys_device_get_state(uint8_t device_id, sys_device_runtime_state_t *out);
err_h sys_device_probe(uint8_t device_id);
err_h sys_device_recover(uint8_t device_id);
```

`sys_device_fault_config_snapshot_t` contains config, all rules, rule count and revision. Store rules/config together under the manager configuration lock. Start revision at 1 and increment only on committed updates. Return a copied snapshot.

- Replace `sys_device_t.state` with `runtime`; replace the three old policy fields with `fault_config` and its revision. Update every direct state access and all state macros; use explicit lifecycle equality checks rather than `>=` if adding lifecycle values later.
- Delete `ops.error_handler` and all adapter severity-classification hooks. Add `err_h (*probe)(void *)`. Probe must perform an actual bounded read; a successful no-op is not health recovery.
- Make `SYS_DEVICE_CREATE(cls, cfg)` evaluate `cfg` once and pass `cfg->device_id`, `&cfg->fault_policy`, pointer and size. Put `sys_device_policy_config_t fault_policy` after `device_id` in each typed config. Its role, actions and matrix must be supplied explicitly.
- Delete `sys_device_report_error()` declaration, implementation and callers. Device boundaries use `SE_handle_incident()` with explicit context; publication uses `SE_report()`.

### 13.3 Capacity defaults

Introduce these named configuration options; all are bounded and included in a static-memory size report:

| Option | Initial default |
| --- | --- |
| `CONFIG_SE_NODE_COUNT` | 128 |
| `CONFIG_SE_INCIDENT_COUNT` | 32 |
| `CONFIG_SE_MAX_CHAIN_NODES` | 16 |
| `CONFIG_SE_ISR_RECORD_COUNT` | 8 |
| `CONFIG_SYS_DEV_FAULT_RULES_MAX` | 8 per installed instance |
| `CONFIG_VM_ERROR_FILTERS_MAX` | 8 |
| `CONFIG_VM_ERROR_OBJECT_SLOTS` | 8 |
| `CONFIG_SYS_FAULT_QUIESCE_MS` | 50 |
| `CONFIG_SYS_FAULT_DEVICE_MS` | 50 per freeze/probe attempt |

Derive node payload capacity from the largest registered payload using compile-time expressions; fail compilation if a payload no longer fits. These timing values are initial software limits, not a hardware response-time guarantee. Every involved driver must accept and obey the remaining deadline; timing a blocking call after it returns does not enforce a deadline.

## 14. Exact error allocation, wrapping, and reporting edits

### `sys_error.c` / `sys_error.h`

1. Remove `err_buffer`, `head_idx`, wraparound allocation and comments promising never to free errors. Use a free-list-backed static node pool, with node and incident references protected by a short lock. No allocation critical section may call a logging/error macro.
2. Keep `tag`, `owner`, `next_cause` and aligned payload access, adding private refcount/incident linkage in pool storage. Every live cause link owns one reference. Sharing nodes never permits changing existing node payloads or cause links.
3. Replace direct allocate-then-write macros with `SE_create(tag, owner, payload, payload_size)` and `SE_wrap_take(cause, tag, owner, payload, payload_size)`. Their typed macro wrappers construct the payload on the stack and pass it for copying. `SE_wrap_take` consumes cause on every path; if wrapper capacity is exhausted, return the original owned cause with a wrapper-loss flag instead of losing the original chain.
4. If creation capacity is exhausted, return an immutable non-NULL pool-exhaustion sentinel and atomically set a reserved critical pending record. Retain/release are no-ops for this sentinel. It cannot accept payload writes or normal mutable incident state. The coordinator drains the reserved pending count directly, so repeated exhaustion is not silently deduplicated as one permanent incident.
5. Change `SE_EMIT_ERR` to create → report → release; `SE_ORIGIN_CALL` evaluates once → report → release. `SE_RET_ERR` transfers its newly owned result. `SE_RET_IF_ERR` transfers the owned returned result. `SE_PASS_ON_ERR` consumes it into a wrapper and transfers the wrapper.
6. `SE_SET_ERR` must not silently overwrite an owned handle. Add `SE_replace_error(err_h *slot, err_h owned)` which releases the previous value; initialize all such slots to NULL.
7. `SE_error_root()` borrows the chain, walks no more than the configured bound, and returns a borrowed node or NULL for malformed input. Provide a traversal status output/helper for callers that need to distinguish invalid input from an empty chain.
8. Response diagnostics attach through incident-owned diagnostic records, never by modifying a published cause chain. Avoid reference cycles: diagnostics contain the parent incident ID, not an owning reference back to the parent chain.

### `sys_error_handler.c`

Delete queue/task declarations, `sys_error_handler_task()`, FreeRTOS queue dependency and task creation. `SE_init()` initializes fixed storage/hooks/counters idempotently before producers start.

Implement `SE_report()` in this order:

```text
retain borrowed chain for this call
merge outer context without replacing originating device/operation
apply policy if NEW; if ACTIVE, mark publication pending and retain outer chain
after policy becomes DONE, atomically claim ordinary publication
snapshot incident info; encode complete chain into caller-owned bounded buffer
attempt nonblocking BLE enqueue; update failure counter
emit configured synchronous serial trace
deliver filtered error callback with zero generic action IDs
release local chain and pending publication reference, if any
```

The task completing an ACTIVE policy processes pending publication after releasing coordinator locks. Keep only the most complete registered outer chain for an incident; replace the pending reference safely. This avoids waiting on the current response and leaking unpublished incidents. A later call after publication is an idempotent no-op; callers must therefore publish only at real terminal boundaries.

Audit manual calls in `runit.c`, `vm_bench.c`, `vm_override.c`, `vm_exec.c`, `sys_actions.c`, `sys_ble_stack.c`, and selftests. In particular, `runit_run_boot_steps()` reports a borrowed error then returns its still-owned handle: do not release it before return. Its eventual top-level owner must release it.

## 15. Exact callback and VM queue edits

### 15.1 `sys_callbacks.h`

Append `CALLBACK_ERROR = 5`, move `CALLBACK_MAX` to 6, retain all existing event values. Add:

```c
typedef struct {
  se_incident_id_t incident;
  err_h chain;
} error_event_t;
// Add error_event_t error to cb_event_t.event.

void sys_callback_event_retain(const cb_event_t *event);
void sys_callback_event_release(cb_event_t *event);
bool sys_callback_try_trigger(const cb_event_t *event);
```

Exact ownership contract:

- Input to `sys_callback_try_trigger()` is borrowed. Success adds one reference owned by the queued copy; failure adds none. Caller ownership never transfers.
- Queue receive moves that queue-owned reference to the local event value without another retain.
- A route function receives a borrowed event. It must retain before storing anything past its return.
- `sys_callback_event_release()` releases only ERROR chain payloads and sets the local chain field to NULL; other event types are no-ops.
- ISR callers may enqueue existing scalar IO/PWR/BLE events. Reject `CALLBACK_ERROR` from ISR through this API; ISR errors use the dedicated mailbox. `sys_callback_try_trigger()` returns false without allocating an error on rejection/full queue.
- Delete `sys_callback_trigger()` and rewrite every producer to call `sys_callback_try_trigger()`. NULL, invalid context and queue-full return false without allocating. Task callers that require a diagnostic explicitly report `ERR_CALLBACK_QUEUE_FULL` for known saturation; ISR/error-notification callers update counters only. No forwarding API remains.

### 15.2 `sys_callbacks.c`

Refactor `sys_cb_task()` so every successfully dequeued event reaches one cleanup point, including OWN_FUNC and missing-action-executor branches. Route dispatch remains before generic actions for existing event types.

```text
receive owned event
if OWN_FUNC: call/report/release returned error; jump to cleanup
for each selected registered route: call route with borrowed event
if ERROR: skip generic actions; jump to cleanup
execute existing dynamic/static actions with report/release of each result
cleanup: sys_callback_event_release(&event)
```

Retain before queue insertion; release the temporary retained reference if insertion fails. Implement retain outside the queue's internal lock, with valid source ownership covering the whole operation. A route mask selecting no installed routes still reaches cleanup. Error events must not carry executable function pointers or action IDs.

### 15.3 `vm_event.c` / `vm_event.h`

- `vm_event_post()` takes a borrowed event; retain for the queued copy and release on failed send.
- Before replacing `s_cycle`, release each previous cycle event. Receiving into `s_cycle` moves the queue's reference to the cycle slot.
- Replace raw `xQueueReset(s_vm_event_q)` with synchronized draining and release. Use an event-generation counter to reject/release posts racing with reset; serialize posting and reset admission with a short lock, never blocking while holding it.
- Release current snapshot records during reset, then set count to zero. All view accessors document that returned events are borrowed until next drain/reset.
- Make dropped-count updates synchronized, not unsafely shared `volatile` increments. Error-event drops only update counters; reporting the overflow must not produce another error event indefinitely.
- Keep the first-critical record outside `s_cycle` so reset of ordinary events cannot discard active global fault evidence.

### 15.4 VM error objects without changing object type encoding

Use the existing `VM_OBJ_U32` type for an opaque error token; do not add a new primitive type to `vm_obj_t_e` in this change. Add `components/VM/core/errors/vm_error_object.c/.h`:

```c
bool vm_error_capture(const cb_event_t *event, uint32_t *out_token);
bool vm_error_get_info(uint32_t token, se_incident_info_t *out);
bool vm_error_get_node(uint32_t token, uint8_t index, se_node_view_t *out);
void vm_error_release(uint32_t token);
void vm_error_objects_reset(void);
```

Token layout: low 8 bits = slot+1 (zero invalid), high 24 bits = generation. Validate both on every read; retire a slot for the current boot if its generation would wrap. Slot capture retains the full chain; release/reset drops it. These are explicit handle objects: copying the U32 token alone does not create a second owning reference. Define object lifetime by capture/release/reset, not generic U32 destruction. Program reset/unload invokes `vm_error_objects_reset()` and invalidates every old token; deleting a generic U32 variable does not automatically release a slot.

Add a dedicated error-access block through `vm_blocks_table.c` supporting CAPTURE_EVENT, READ_INFO, READ_NODE, RELEASE operations. Match its concrete port descriptors to existing block registration conventions; require U32 token/index inputs and bounded U8 payload output. Failed lookup returns a precise invalid-token error, never dereferences an expired node. A node view is borrowed only during the current read call; copy payload bytes into the supplied VM output object before returning.

Add a per-block severity side table indexed by block ID, initialized to WARNING on creation and cleared on deletion/program reset. `vm_block_set_fault_severity(block_idx, severity)` validates block existence and severity. This selects the separate command approach in section 8; do not change the loader's existing `on_error` byte or structure size.

## 16. Exact dispatch and safety-coordinator edits

### 16.1 Operation mapping

Change `SYS_DEV_DISPATCH` to accept an explicit `operation` argument before contract ID. Update all call sites:

| API operation | Context |
| --- | --- |
| IO get level/get voltage; power monitor get voltage/current; USB-PD get limits/list | READ |
| IO set level/toggle/set voltage/set PWM duty; VREG enable/voltage/current; USB-PD set | WRITE |
| IO set mode/set PWM frequency; all add-callback operations | CONFIGURE |
| install / reset / freeze / recovery probe | INSTALL / RESET / FREEZE / PROBE respectively |
| suspend/resume/sync/uninstall | CONFIGURE, retaining precise existing owner/tag |
| budget/rail policy events | POWER |

Validation and adapter failures both retain device/operation context. A validation rejection does not count as a failed physical transaction. Reads rejected solely due to existing isolation do not create a new physical fault occurrence or replay freeze/actions.

Introduce manager dispatch enter/leave helpers with task-local nesting depth. At the first entry, admit the operation and count it as in flight. Nested dependency calls share that admission and carry their own device context. On return, wrap failures and retain the deepest originating device. At final leave, decrement admission and release all driver locks before calling policy. Mark critical pending/inhibit as soon as the cause is recognized; delay blocking response work until safe unwinding. Audit direct low-level driver users that bypass this wrapper.

### 16.2 Freeze implementation

Stop using `SYS_DEV_LIFECYCLE_OP_ALL` for freeze. Implement a dedicated reverse-order sweep with a fixed-size failed-device bitset and first diagnostic per failed stage. Do not change unrelated reset/uninstall sweep semantics incidentally.

Set manager isolation before attempting an adapter freeze. Add an explicit class capability flag `requires_freeze`; every class must declare it. Reject installation if it is true and the freeze hook is absent. Read-only devices explicitly declaring false use manager isolation as their complete configured response. A required hook disappearing at runtime is an invariant failure, recorded as failed response; no substitute operation is attempted.

### 16.3 `runit_fault_control.c` (new)

Create the deferred safety-control task described earlier; do not rely on `sys_cb_task` or the hung VM supervisor. Register its handle before enabling watchdog/interrupt sources. Its fixed mailbox carries ISR records and watchdog requests only, plus notification bits for reserved emergency state; ordinary task-context device errors still execute synchronously.

Expose application hooks for latch/inhibit, global freeze, local isolate, scoped action, and response-result storage. Hook registration is done before board device installation. Boot failure uses the same incident coordinator; remove the independent second freeze from `runit_run_boot_steps()` after a handled critical incident.

Add `vm_exec_fault_latch_from_isr()` with an output wake flag and ISR critical-section primitives. Factor task/ISR latch field updates into a small shared helper that assumes the corresponding lock is held. Never call `vm_exec_stop()` from that helper. Keep the existing stopped/quiescent acknowledgment checks and extend them to reject acknowledgment while safety response is ACTIVE.

Use a per-task response-depth/context slot. Normal device errors generated while executing a response become response diagnostics; do not invoke configured actions on those errors. Reject dynamic critical bindings unless the action registry marks the target as callable without normal VM execution; add that capability to registration/readback. Board policy constructors must specify each action as STATIC, DYNAMIC or NONE; absent bindings reject configuration. Mandatory freeze/stop operates independently.

Board initial assignments: keep GPIO, TCA6424A, ADS7128, TPS55289 instances, INA3221 and AP33772S SYSTEM_CRITICAL because this configuration uses them on the mainboard power/control path; set PCA9685 and optional DAC53202 IMPORTANT. These are proposed board defaults, not inferred proof of electrical safety; review them against the board schematic before target fault-injection acceptance. Test fixtures explicitly use TEST.

## 17. Exact protocol additions

The following IDs are proposed allocations in system-contract class `0x01`; the inspected dispatcher currently has no `0x50`–`0x59` entries. Recheck collisions when implementing against a later branch. All integers are little-endian. Prefix every new command payload with `u8 version=2, u16 request_id`; request ID is chosen by the host.

| Class/command | Payload after version/request ID | Exact payload length |
| --- | --- | --- |
| `01 50` replace device rules | `u8 device, u8 count, rule[count]` | `5 + 5*count` |
| `01 51` read device fault config/state | `u8 device` | 4 |
| `01 52` replace VM filters | `u8 count, filter[count]` | `4 + 8*count` |
| `01 53` read VM filters | none | 3 |
| `01 54` intentional offline | `u32 session, u8 allow` | 8 |
| `01 55` read critical fault | none | 3 |
| `01 56` read drop counters | none | 3 |
| `01 57` probe device | `u8 device` | 4 |
| `01 58` recover device | `u8 device` | 4 |
| `01 59` set block severity | `u16 block, u8 severity` | 6 |

Rule record (5 bytes): operation, match-kind, match-value u16, severity. Filter record (8 bytes): source (255=any), any-device (0/1), device, severity-mask, operation (0=any), match-kind, match-value u16. Severity mask bit N selects severity N. ANY match requires value=0. Enforce configured maximum counts and reject unknown enum values/reserved mask bits.

The `01 1A` packet uses the complete 89-byte layout in section 8, verified using `01 51` readback. Device install commands `01 40`–`01 47` use `version=2, device_id, role, six action bytes, 80 matrix bytes`, followed by the device's hardware-specific scalar fields. Define each new exact size in the codec schema and accept only that layout. Installation supplies a complete policy before any hardware operation.

### Responses

Use the existing `PACKET_HEADER_TX` transport slot. Allocate payload prefix `01 7F` for fault-control responses (distinct from transport framing). Payload:

```text
u8 class=0x01; u8 command=0x7F; u8 version=2;
u16 request_id; u8 request_command; u8 status;
u16 body_length; u8 body[body_length];
```

Status: OK=0, INVALID=1, NOT_FOUND=2, BUSY=3, FAILED=4. An error status body is `u16 root_tag` (zero only if no chain could be created); normal error telemetry still carries any detailed chain. All mutation success responses have empty bodies except intentional-offline, which returns the applied u32 session and u8 allow. Queue the acknowledgment before a client voluntarily disconnects; the client must wait to receive it. A dropped response does not roll back an already committed mutation; readback/retry must be idempotent.

Read response bodies:

- `51`: device u8, lifecycle u8, health u8, isolated u8, fault_count u32, last boot_id u32, last sequence u32, revision u32, role u8, six scope/ID bytes, 80 matrix bytes, rule_count u8, rules. Fixed prefix is 108 bytes before rules. Serialize the explicit configured matrix; the returned exact-tag rules and matrix reconstruct the compiled policy.
- `53`: count u8 followed by eight-byte filter records.
- `55`: present u8; if present, full v2 error record as specified below. Absence has no trailing bytes.
- `56`: six u32 saturating counters in order: node/incident storage exhaustion, lost ISR detail, callback queue drop, VM event drop, BLE packet drop, malformed/incomplete chain.

Variable-length commands need explicit handlers before the existing fixed-struct X-macro cases. Check `len` before reading count, then require exact count-derived length. Decode to aligned native temporary records; perform one API mutation after complete validation. Do not feed variable-length payloads into the current `convert_to_packet(..., sizeof(packet))` path.

Golden request examples, excluding transport framing:

```text
01 50 02 34 12 07 00
# request 0x1234: clear all rules for device 7

01 52 02 35 12 01 FF 01 00 07 00 00 00 00
# one filter: any source/device/operation/tag, all three severities

01 59 02 36 12 2A 00 01
# block 42: WARNING
```

Complete `01 1A` golden vector builder: `bytes.fromhex("01 1A 02 07 02 00 03 FF 00 FF 00") + bytes([1] * 80)`. This produces 91 bytes including class/command, configuring device 7 as USER, critical STATIC action 3, warning/notice NONE, and every configurable matrix cell as WARNING. Mandatory critical rules remain enforced independently. A header without the 80 matrix bytes is invalid.

## 18. Exact telemetry v2 layout

Set `ENC_SYS_ERRORS_FMT_VERSION=2`, `ENC_SYS_ERRORS_HDR_LEN=44`. Serialize fields explicitly; retain existing five-byte node headers and original payload bytes.

| Byte offset | Type | Field |
| --- | --- | --- |
| 0 | u8 | version=2 |
| 1 | u8 | node_count present |
| 2 | u8 | depth observed, capped at 255 |
| 3 | u8 | flags |
| 4 | u32 | boot_id |
| 8 | u32 | incident sequence |
| 12 | u8 | source |
| 13 | u8 | operation |
| 14 | u8 | severity |
| 15 | u8 | response outcome |
| 16 | u8 | device_id; zero if absent |
| 17 | u8 | block_type; zero if absent |
| 18 | u16 | block_idx; zero if absent |
| 20 | u32 | policy revision; zero for non-device default |
| 24 | u32 | incident occurrence count |
| 28 | u32 | parent boot_id; zero if absent |
| 32 | u32 | parent sequence; zero if absent |
| 36 | u32 | response stage failure mask |
| 40 | u16 | root tag captured in stable incident metadata |
| 42 | u16 | root owner captured in stable incident metadata |
| 44 onward | records | existing `u8 payload_len, u16 tag, u16 owner, payload` |

Flags: bit0 has-device, bit1 has-block, bit2 packet truncated, bit3 depth limit reached, bit4 storage/wrapper loss, bit5 malformed chain, bit6 has-parent, bit7 reserved=0. Depth is only the observed depth; never claim an exact original length after loss/limit. Root metadata remains available even if inner nodes do not fit the packet. Stage mask bits retain existing fault-stage indices CALLBACK=0, VM_STOP=1, FREEZE=2, ACTION=3, adding QUIESCE=4.

When no ordinary node is available (reserved emergency record), allow node_count=0, set storage-loss and include captured root metadata. An empty ordinary chain is not otherwise a reportable error. Configure maximum packet size to be at least 44 plus one maximum node record; bound large chains and set truncation rather than fragmenting in this iteration.

`decodeBinaryErrorPacket()` accepts only version 2 and the new schema. Delete the previous parser and simulated packet format. Bounds-check every field and node; expose `complete=false` on any loss flag or missing node bytes. Fix the stale top-of-file transport/class description in `stream_decoder.js`: the board currently defines `PACKET_HEADER_ERRORS` as `0x04`; use the actual board framing rather than the comment's `0x05` assumption.

## 19. Exact host and build edits

| File | Required edit |
| --- | --- |
| `Python/PythonRunIT/decoder_types.py` | Replace `packet_sys_device_set_error_handling_t` fields; update 0x40–0x47 install types; register 0x50–0x59 fixed headers and explicit variable payload builders. |
| `Python/PythonRunIT/sync_decoders_from_c.py` | Preserve new fields and version defaults during generation; handle variable-size commands explicitly rather than generating fake fixed ctypes arrays. |
| `Python/PythonRunIT/vm/packets.py` | Add block-severity command builder and request/response correlation for fault commands. |
| `Python/PythonRunIT/vm/program.py`, `vm_types.py` | Expose separate block fault severity and U32 error-handle block configuration without changing existing on_error semantics. |
| `Python/PythonRunIT/ble_client.py` | Track session returned by the connection/session query path, send offline intent and wait for matching acknowledgment before planned disconnect; do not set intent for accidental loss. |
| `Python/PythonRunIT/web_ui/stream_decoder.js` | Replace parser with version-2-only decoding, incident metadata, completion flags, new responses, regenerated tag schema and new simulated packet builder. |
| `Python/PythonRunIT/web_ui/app.js`, `runit_gui.py` | Replace old booleans with role/rules/scoped actions and subscriptions; show health separately from lifecycle; render full chains and partial-response status. |
| `Python/PythonRunIT/web_ui/server.py` | Pass correlated replies and new command payloads through existing transport routes; validate UI-provided values before encoding. |
| `components/sys_errors/CMakeLists.txt` | Add `sys_error_policy.c` and pool source if split; avoid a VM dependency. |
| VM/runit component source lists | Register `vm_error_object.c` and `runit_fault_control.c`; add required direct include dependencies. |

Session discovery for `01 54`: define session=0/allow=0 as a read-only query returning the current session/flag; ordinary set/cancel requires the returned nonzero session. Allocate a fresh nonzero session counter on each successful BLE connection, invalidating it on disconnect. A query while disconnected returns NOT_FOUND. Implement this through a BLE API called by the command handler; do not read private BLE globals from the decoder.

Add host protocol tests under `Python/PythonRunIT/tests/test_fault_protocol.py` using standard-library `unittest`. Add browser decoder fixtures driven by its export mechanism; compare exact bytes and decoded values for version 2, rejected unsupported versions, truncation, unknown tags, all filters, complete matrices and all response bodies.

## 20. Concrete implementation review checklist

- [ ] No raw `err_h` crosses a queue or persists in a VM slot without an owned reference.
- [ ] Every successful queue receive has a matching release on every branch; reset drains owned payloads.
- [ ] No error constructor writes into an allocation-failure sentinel.
- [ ] Creating a wrapper cannot destroy or lose the original owned cause on exhaustion.
- [ ] `SYS_DEV_DISPATCH` and manual power writes use explicit operations and final-unwind handling.
- [ ] Callback configuration cannot disable physical safety; callback event action IDs are zero.
- [ ] Freeze attempts all devices and records missing required hooks and expired deadlines.
- [ ] No coordinator lock spans VM barriers, driver operations, logging, actions, or queueing.
- [ ] Ordinary task policy remains synchronous; only constrained ISR/watchdog contexts use the control mailbox.
- [ ] Tests demonstrate critical response while the VM is stuck and while another task reports a second independent incident.
- [ ] Firmware emits a byte-for-byte golden v2 record matching both Python and JS decoders.
- [ ] Every new command validates exact length before field access and validates all entries before committing.
- [ ] Readback confirms runtime role/rules/actions/filter changes and policy revision coherence.
- [ ] Delete obsolete flags, bare three-action configuration, overwrite-ring claims, error-handler task APIs, previous protocol parsers, adapter classifier chains and inherited action bindings from implementation and product documentation.

## 21. Completion evidence to attach to the implementation

Record the actual build command/environment, target firmware build result, selftest result, host test result, measured static RAM increase, maximum observed response latency, injected failure conditions and all hardware checks not performed. Include one captured device→block failure showing a single incident ID/action and the completed chain, one intentional versus accidental disconnect pair, and one freeze-failure capture showing later devices still attempted. Do not describe the implementation as hardware-safe solely because host/unit tests passed.
