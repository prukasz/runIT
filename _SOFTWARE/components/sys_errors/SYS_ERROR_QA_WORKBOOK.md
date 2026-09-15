# System Error & Device Policy Architecture — Questionnaire Workbook

> **Instructions**: Fill in your answers, check options (`[x]`), or type your preferences under each question. Once completed, save the file and let me know—the final architecture and implementation will be generated directly from your answers.

---

## Part 1: Execution Model & Fast-Path

### Q1. Synchronous Direct Execution vs. Background FreeRTOS Task
When an error reaches an origin call (e.g. `SE_ORIGIN_CALL`), how should the safety response (VM stop, freezing devices, safe action) execute?
- [ ] **A:** 100% direct and synchronous on the calling thread. Delete `sys_error_handler_task` and `s_err_queue` completely.
- [ ] **B:** Hybrid: Direct synchronous execution for emergency safe state (`CRITICAL`), but keep the queue/task for non-critical logging & telemetry.
- [ ] **C:** Asynchronous: All errors queue to `s_err_queue`; the background task handles both telemetry and policy.
- **Your Answer / Notes:** 

---

### Q2. Outbound Telemetry (Serial Trace & BLE Packet Sink)
If the background task is removed or bypassed, how should serial logging (`ESP_LOGE`) and BLE telemetry packets be dispatched?
- [ ] **A:** Serial prints inline synchronously; BLE packet is handed to the existing non-blocking TX ring buffer (drops if full, zero blocking).
- [ ] **B:** Serial prints inline synchronously; remove BLE error packet streaming entirely.
- [ ] **C:** Keep a dedicated lightweight telemetry task/queue strictly for outbound BLE transmission, completely separated from the safety path.
- **Your Answer / Notes:** 

---

### Q3. Universal Macro Entry Points
Which macros should trigger the error policy?
- [ ] **A:** `SE_ORIGIN_CALL(call)` is the top-level boundary. Add `SE_REPORT_DEV_ERR(dev_id, tag, ...)` for explicit reporting.
- [ ] **B:** Reroute `SE_push_to_handler()` directly so all existing `SE_EMIT_ERR` and `SE_ORIGIN_CALL` calls run the direct handler automatically.
- [ ] **C:** Both: `SE_ORIGIN_CALL` catches return values, while `SE_EMIT_ERR` reports background/asynchronous events.
- **Your Answer / Notes:** 

---

### Q4. Handling Errors Originating Inside an ISR
An ISR cannot take FreeRTOS mutexes or perform I2C bus transactions. When an error is detected inside an ISR:
- [ ] **A:** Latch the fault status in VM (`vm_exec_fault_latch`) and trigger a task notification to wake the main task to execute the full freeze.
- [ ] **B:** Toggle a dedicated hardware emergency/enable GPIO pin directly from the ISR, then defer the rest of the software teardown.
- [ ] **C:** Immediately trigger a kernel reset (`esp_restart()`).
- **Your Answer / Notes:** 

---

### Q5. Re-entrancy & Infinite Loop Protection
If `sys_device_freeze_all()` fails, or an emergency action itself fails, how should the system prevent an infinite error loop?
- [ ] **A:** A static boolean `s_in_policy` drops any nested policy calls. Any error tagged `ERR_DEV_FAULT_RESPONSE_FAILED` is logged to serial and never re-evaluated.
- [ ] **B:** Allow up to 2 nested retries, then trigger an immediate hardware reboot (`esp_restart()`).
- **Your Answer / Notes:** 

---

## Part 2: Device Criticality Roles & Classification

### Q6. Device Role Taxonomy
What roles should devices have to determine their baseline criticality?
- [ ] **A:** Four roles: `SYSTEM_CRITICAL` (power/safety), `IMPORTANT` (actuators/motors), `USER` (UI/sensors), `TEST` (fixtures/mocks).
- [ ] **B:** Three roles: `CRITICAL` (system stops), `NON_CRITICAL` (device isolated, system runs), `TEST` (log only).
- [ ] **C:** Custom roles (specify below).
- **Your Answer / Notes:** 

---

### Q7. Where is a Device's Role Declared?
- [ ] **A:** Dynamic instance config: Specified per-instance during install via its config struct (`cfg->role`). (e.g. an expander can be CRITICAL on mainboard, but USER on an expansion header).
- [ ] **B:** Static class definition: Fixed in `sys_device_class_t` per device type.
- [ ] **C:** Both: Static default in `cls`, which can be overridden in `cfg`.
- **Your Answer / Notes:** 

---

### Q8. Runtime Role Reconfiguration
Should remote clients (Python GUI / BLE) be able to reassign a device's role at runtime via a command packet?
- [ ] **A:** Yes, allow dynamic role modification via a contract packet.
- [ ] **B:** No, immutable after `sys_device_install_cfg()`.
- **Your Answer / Notes:** 

---

### Q9. Adapter Hook (`cls->ops.classify_error`) vs. Central Fallback Classifier
How should device-specific knowledge interact with the system-level classifier?
- [ ] **A:** Tier 1: Device adapter's `classify_error` hook runs first. If NULL or returning `UNSPECIFIED`, fall back to Tier 2 (Central Role + Tag Matrix).
- [ ] **B:** Central classifier has absolute authority; do not allow device adapters to override severity.
- [ ] **C:** Only device adapters classify their errors; central classifier only handles non-device errors.
- **Your Answer / Notes:** 

---

### Q10. Cleaning `sys_device.h:L106-L108`
In `sys_device.h`, we currently have `uint8_t actions[3]; bool use_error_handler; bool generate_error_callback;`. How should this be cleaned up?
- [ ] **A:** Remove both booleans completely. Replace with `role`, `health`, and optional `action_overrides[3]` (0 = use role default action).
- [ ] **B:** Remove booleans and remove `actions[3]` entirely. Actions are managed purely at the system level.
- **Your Answer / Notes:** 

---

## Part 3: Fault Qualification, Debouncing & Health States

### Q11. Transient Physical Bus Glitches (Debouncing)
On I2C/SPI buses, occasional NACKs or noise glitches happen. Should 1 glitch immediately trigger policy?
- [ ] **A:** Yes, any communication error immediately trips the assigned role severity.
- [ ] **B:** No, debounce: require $N$ consecutive failures (e.g. 2 or 3) before escalating from a transient `NOTICE` to a confirmed fault.
- **Your Answer / Notes:** 

---

### Q12. Where Should Debounce / Retry Logic Live?
- [ ] **A:** Centralized in the device manager / error policy coordinator (tracking `dev->consecutive_failures`).
- [ ] **B:** Inside each individual driver/adapter (driver retries $N$ times before returning an error).
- **Your Answer / Notes:** 

---

### Q13. Explicit Device Health States
Should `sys_device_t` track a formal runtime health state enum?
```c
typedef enum { SYS_DEV_HEALTH_OK, SYS_DEV_HEALTH_DEGRADED, SYS_DEV_HEALTH_FAULTED } sys_dev_health_e;
```
- [ ] **A:** Yes, track health status and expose it to the VM and telemetry.
- [ ] **B:** No, device state (`INSTALLED`, `SUSPENDED`) is sufficient; do not add a separate health enum.
- **Your Answer / Notes:** 

---

### Q14. Fault Latching vs. Self-Healing
If a faulted device starts responding normally again:
- [ ] **A:** Self-healing: automatically clear health back to `OK` on the first successful transaction.
- [ ] **B:** Latched: once marked `FAULTED`, it remains faulted until explicitly reset via command or `vm_exec_fault_acknowledge()`.
- [ ] **C:** Self-healing for `USER`/`TEST` devices, but strictly latched for `SYSTEM_CRITICAL` devices.
- **Your Answer / Notes:** 

---

## Part 4: Safety Reaction, Safe State & Device Freezing

### Q15. Order of Operations for `CRITICAL` Safe State
When a `CRITICAL` fault occurs, what is the mandatory sequence?
- [ ] **A:** (1) Latch fault in VM &rarr; (2) Stop VM execution pass &rarr; (3) Freeze all devices (`sys_device_freeze_all`) &rarr; (4) Run registered emergency action.
- [ ] **B:** (1) Freeze all devices immediately &rarr; (2) Stop VM &rarr; (3) Run emergency action &rarr; (4) Latch fault.
- [ ] **C:** Custom order (specify below).
- **Your Answer / Notes:** 

---

### Q16. Global Freeze vs. Localized Device Isolation
When an `IMPORTANT` or `USER` device fails:
- [ ] **A:** Localized isolation: call `sys_device_freeze(dev_id)` for that device only. The rest of the board and VM keep running.
- [ ] **B:** Global freeze: any device failure always sweeps and freezes all devices on the board.
- **Your Answer / Notes:** 

---

### Q17. VM Behavior When a Non-Critical Device Faults
If a `USER` peripheral (e.g. auxiliary sensor) is marked `FAULTED`, what should happen when a running VM block tries to read/write it?
- [ ] **A:** Block execution fails cleanly with an error code; user VM script error-handling handles it.
- [ ] **B:** Block returns a safe fallback dummy value (0 / NaN) and VM execution continues smoothly.
- [ ] **C:** Immediately pause/stop the VM, but do not freeze other hardware.
- **Your Answer / Notes:** 

---

### Q18. Emergency Action Failure Escalation
If the emergency action invoked during safe state itself fails:
- [ ] **A:** Log to serial and latch `ERR_DEV_FAULT_RESPONSE_FAILED`; board remains in safe state.
- [ ] **B:** Escalate to immediate hardware reset (`esp_restart()`).
- **Your Answer / Notes:** 

---

## Part 5: Merging Non-Device Error Sources

### Q19. VM Execution Watchdog (`ERR_VM_EXEC_BLOCK_HUNG`)
When the visual block execution watchdog trips (e.g. infinite loop in user block script):
- [ ] **A:** Escalates to `CRITICAL`: triggers full safe state (stops VM, freezes all devices, latches fault).
- [ ] **B:** Resets the VM engine only, keeping hardware in its current state.
- **Your Answer / Notes:** 

---

### Q20. VM Script Runtime Errors (Div-by-zero, Type Mismatches)
When an arithmetic error or type mismatch occurs inside a running VM block:
- [ ] **A:** Treated as `WARNING`: aborts the current block pass and latches error, but does NOT freeze hardware outputs.
- [ ] **B:** Treated as `CRITICAL`: triggers full hardware safe state.
- [ ] **C:** Treated as `NOTICE`: logs error and block produces 0, pass continues.
- **Your Answer / Notes:** 

---

### Q21. VM Memory Exhaustion (`ERR_VM_ARENA_FULL`)
If the VM heap/arena runs out of memory while allocating blocks or dynamic objects:
- [ ] **A:** `CRITICAL`: system safe state.
- [ ] **B:** `WARNING`: refuse new blocks, stop VM execution pass cleanly.
- **Your Answer / Notes:** 

---

### Q22. Power Budget Exceeded (`ERR_POWER_BUDGET_EXCEEDED`)
When total system current/power budget is exceeded:
- [ ] **A:** Load Shedding (`WARNING`): automatically suspend/freeze `TEST` and `USER` devices to restore power budget.
- [ ] **B:** Emergency Safe State (`CRITICAL`): immediately stop VM and freeze all devices.
- [ ] **C:** Notify VM only: user program decides which loads to turn off.
- **Your Answer / Notes:** 

---

### Q23. Hardware Power Rail Fault / Brownout
If a hardware power monitor detects an over-voltage, severe under-voltage, or PMIC thermal trip:
- [ ] **A:** Immediate `CRITICAL`: synchronous hardware output disable + full safe state.
- [ ] **B:** Attempt rail restart up to 3 times before failing.
- **Your Answer / Notes:** 

---

### Q24. I2C Bus Wedged (SDA / SCL Stuck Low)
If an I2C transaction times out and the bus is wedged low:
- [ ] **A:** Attempt bus recovery (clock toggles). If recovered &rarr; `NOTICE`. If permanently stuck &rarr; `CRITICAL` safe state.
- [ ] **B:** Immediately trigger `CRITICAL` safe state without recovery.
- **Your Answer / Notes:** 

---

### Q25. BLE Disconnects & Corrupted Interface Frames
When a remote BLE client disconnects, or a corrupted frame arrives over UART/Interface:
- [ ] **A:** Strictly `NOTICE`: log/telemetry only. Zero impact on running VM or devices.
- [ ] **B:** Disconnect triggers a configurable `sys_action` (e.g. stop VM on communication loss).
- **Your Answer / Notes:** 

---

### Q26. Heap Exhaustion (`ERR_BASE_NO_MEM`) & Platform Invalid State
If `malloc()` or an internal allocator fails anywhere in the firmware:
- [ ] **A:** `CRITICAL`: immediate safe state.
- [ ] **B:** Panic / reboot (`esp_restart()`).
- **Your Answer / Notes:** 

---

## Part 6: Wire Protocol, Telemetry & Testing

### Q27. Updating Wire Packet `0x1A` (`packet_sys_device_set_error_handling_t`)
Packet `0x1A` currently takes `use_error_handler`, `generate_error_callback`, and `actions[3]`. How should the new payload look?
- [ ] **A:** `uint8_t device_id; uint8_t role; uint8_t action_overrides[3];`
- [ ] **B:** `uint8_t device_id; uint8_t role;` (actions configured via separate action binding commands).
- **Your Answer / Notes:** 

---

### Q28. Telemetry Error Packet Content
When an error chain is streamed over BLE to the host computer / PythonRunIT, what header information must it carry?
- [ ] **A:** Standard encoded chain: Root Tag + Owner + Payload + Severity + Device ID.
- [ ] **B:** Compact: 4-byte header (Device ID, Severity, Tag, Owner).
- **Your Answer / Notes:** 

---

### Q29. Static vs. Dynamic Action Invocations in Safe State
When `runit_device_error_policy` invokes an action:
- [ ] **A:** Try Static actions first (`SYS_ACTION_SCOPE_STATIC`), then fall back to Dynamic (`SYS_ACTION_SCOPE_DYNAMIC`).
- [ ] **B:** Static actions only for safety-critical responses.
- **Your Answer / Notes:** 

---

### Q30. Selftest Validation Scope (`stage_device_fault.c`)
Which tests are mandatory in the selftest harness?
- [ ] **A:** Full suite:
  1. `SYSTEM_CRITICAL` device failure &rarr; VM stopped, devices frozen, fault latched.
  2. `USER` / `TEST` device failure &rarr; VM remains running, fault counted.
  3. Adapter hook overriding central classification.
  4. Anti-recursion guard when safe action fails.
- [ ] **B:** Minimal: Only test `CRITICAL` safe-state latching and freezing.
- **Your Answer / Notes:** 

---

