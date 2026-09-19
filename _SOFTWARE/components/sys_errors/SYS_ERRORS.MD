# System errors

Update this document when changing the error core, dispatcher, or tag maps.

## Pipeline and ownership

Errors are typed nodes linked through `next_cause`. `NULL` means success.
`SE_ERR_NEW` creates an owned node; `SE_WRAP_ERR` transfers ownership of its cause
into the new chain. Explicit-owner variants use the same allocator/constructors.
Do not share a cause between two owned chains.

```text
create -> wrap while returning -> SE_ORIGIN_CALL / SE_push_to_handler
       -> validate -> outermost node through causes -> diagnostics -> release
```

`SE_push_to_handler` is synchronous and **consumes** its argument, including when
reporting is suspended. Do not inspect, return, or release that handle afterwards.
`SE_send`, the encoder, logging functions, and policy hooks **borrow** their input.
They do not retain it. Their non-NULL return is a separate owned failure.
Use `SE_release(error)` after inspecting/discarding an error yourself.
`SE_IS_OK` / `SE_IS_ERR` only compare; they do not release an error.

```c
err_h error = operation();
if (error) {
  SE_release(SE_send(error)); // diagnostics only; release an encoding failure
  SE_release(error);         // or return error to transfer ownership to caller
}
// At an origin with no further use of the error:
SE_ORIGIN_CALL(operation());
```

## Storage and validation

The shared pool is 1,024 bytes: 32 blocks of 32 bytes, reserved with a 32-bit atomic
bitmap. A node occupies consecutive blocks. A 32-byte start-length table validates
node boundaries and complete tag-sized payloads. Tracking costs 36 bytes. There is
no heap allocation and live nodes are never overwritten. Payloads and nodes remain
8-byte aligned. Maximum concurrent capacity depends on payload sizes (up to 32
small nodes); synchronous handling does not eliminate overlapping producer tasks.

`SE_alloc_bytes` validates tag and exact payload size and returns NULL on exhaustion.
Prefer the constructors: a failed new error returns an immutable `ERR_BASE_NO_MEM`
node; a failed wrap preserves its existing cause, omitting the new context. Never
mutate the exhaustion node. Long-lived errors consume pool capacity until released.
The pool can exhaust without corrupting another chain; it does not guarantee that
all context can be retained during an error storm.

`SE_collect_chain` returns at most `SE_MAX_CHAIN_DEPTH` (16) unique validated nodes,
outermost first, plus a completeness flag. Handler, logger, encoder and root lookup
share this traversal. Cycles, invalid pointers and longer chains terminate safely.
Malformed/incomplete chains are diagnostic-only. `SE_release` frees the complete
pool-backed chain (also beyond the traversal limit) with bounded cycle detection.
Raw pointers are not generation handles: using a handle after consumption/release
remains invalid, even if its address is later allocated again.

## Dispatch policy

- Device attribution uses the tag payload, independently of the reporting owner.
  Device tags, IO pin tags and power-budget tags identify device instances.
  Contract boundaries wrap failures with the affected device ID.
- Visit outermost node to root cause. For a repeated device, use the highest local
  severity among its mentions before the first ignored-device boundary. Run that
  device's policy once, at its first occurrence; pass the node supplying that level.
  The leaf does not silently replace another node's severity.
- Importance NONE stops handling that device and all deeper causes. Earlier
  responses remain in effect; the full chain still goes to diagnostics. To suppress
  an entire incident, the ignored-device context must precede any actionable nodes.
- Noncritical response levels are capped by importance. Critical bypasses that cap
  for enabled devices; NONE still disables it. Tag severities themselves are unchanged.
- Domain hooks receive `(node, full_chain)` for each visited node in that domain.
  Unimplemented weak hooks are optional. `sys_error_hooks.h` is the dispatcher's
  single optional-hook declaration header; map headers only describe errors/owners.
- Unattributed critical errors (where no device in the chain was attributed,
  `handled == 0`) use `sys_system_handle_fault`, never a lookup of device zero.
  The application latches the triggering node and performs global stop/freeze
  once per latch episode. Enabled devices still run their individual actions
  after the global latch has been set. The dispatcher deduplicates devices
  per incident, not across separate incidents.
- Response failures are diagnostics-only and released after sending. A per-task
  handler guard prevents callbacks/actions re-entering policy while handling an
  incident. Hooks must return new owned failures, never the borrowed input chain.

`SE_suspend` / `SE_resume` are nested and task-local. Suppression in one task does
not drop unrelated errors from another task. The synchronous dispatch/logging API
is for task context; it can invoke actions, transport callbacks and VM barriers.

## Logging and telemetry

`SE_set_logging(level, mirror_serial, trace_errors)` controls text logging and the
ESP log interceptor. Text goes to connector 0 and optionally serial. Binary errors
go to connector 1 independently of text tracing. Connector configuration owns
framing, binding and capacity. The disconnected `sys_error_config` API was removed.
`SE_send` does not acknowledge delivery: transport providers have a void send API.
Its returned error describes encoding failure. Log recursion guards are task-local.

The binary format is version **2**:

```text
u8 version=2 | u8 node_count | u8 depth | u32 schema_id LE
repeated: u8 payload_length | u16 tag LE | u16 owner LE | payload bytes
```

`depth > node_count` indicates packet-capacity truncation. Depth 255 means corrupt
or over-depth input (the exact length is unknown). The schema ID is FNV-1a over
ordered NUL-terminated tag names and payload sizes (size XORed once per tag).
It detects reordered/added tags and payload-size changes. A same-size payload-layout
change still requires a format version change. Payloads retain native C alignment
and padding; scalar record headers are explicitly little endian.

The web stream decoder accepts v1 and v2. Its legacy typed tag table applies only
to v1; v2 packets retain IDs, owners, payload bytes and schema ID without applying
an unverified old tag table. Formatted logs still supply human-readable descriptions.
Clients with a matching schema may decode typed payloads.

## Files and verification

- `sys_error.c`: pool, release, bounded traversal, metadata.
- `sys_error_handler.c`: synchronous policy dispatch.
- `sys_error_log.c`: ESP logging hook, formatting, connector send.
- `include/sys_error.h`: constructors and ownership API.
- `include/sys_error_hooks.h`: optional domain hooks.
- `codes/sys_error_codes.h`: aggregate module tag/owner/logger maps.

`python tools/tests/test_sys_errors.py` exercises real core, dispatcher, encoder,
device manager and application policy with host substitutes for hardware/VM/actions.
It needs Zig and Node; see the script for a local toolchain installation. Firmware
selftests also release intentionally discarded errors and test device policy/encoding.
Host tests do not substitute for hardware scheduling/transport tests.

## Error Lifecycle & Call Tree Architecture

### 1. High-Level Flowchart

```mermaid
flowchart TD
    A["Error Origin (Driver / Subsystem / VM)"] -->|"SE_RET_ERR / SE_WRAP_ERR"| B["SE_push_to_handler(err)"]

    subgraph Phase1["Phase 1: Inspection & Chain Collection"]
        B --> C["SE_collect_chain()"]
        C --> D{"in_handler or corrupt?"}
        D -->|Yes| E["Diagnostics Only (Skip Policies)"]
    end

    subgraph Phase2["Phase 2: Attribution & Policy Dispatch"]
        D -->|No| F["Iterate Nodes in Error Chain"]
        F --> G{"Node has Device ID?"}

        %% Device Policy Path
        G -->|Yes| H["sys_device_report_error_with_level()"]
        H --> I{"dev->importance == NONE?"}
        I -->|Yes| J["Drop error (Ignored/Test Device)"]
        I -->|No| K["Clamp severity level & lookup action_id"]
        K --> L["sys_device_app_error_policy()"]
        L --> M{"action_id != 0?"}
        M -->|Yes| N["sys_actions_invoke(action_id)"]
        L --> O{"level == CRITICAL?"}
        O -->|Yes| P["runit_system_fault()"]
        P --> P1["vm_exec_fault_latch()"]
        P --> P2["vm_exec_stop()"]
        P --> P3["sys_device_freeze_all()"]

        %% Domain Fault Hook Path
        F --> Q["dispatch_domain(node, err)"]
        Q --> R["sys_<domain>_handle_fault(node, err)"]
        R -->|VM| R1["sys_vm_handle_fault -> vm_exec_stop()"]
        R -->|BLE| R2["sys_ble_handle_fault -> SYS_BLE_CB(FAILURE)"]
        R -->|Actions| R3["sys_actions_handle_fault -> abort recording"]
        R -->|Interface| R4["sys_interface_handle_fault -> suspend RX"]
        R -->|Power/I2C/IO/Buffers| R5["Hardware containment / bus reset"]

        %% Fallback System Hook
        F --> S{"CRITICAL & Unhandled?"}
        S -->|Yes| T["sys_system_handle_fault()"]
    end

    subgraph Phase3["Phase 3: Baseline Transport & Logging"]
        E --> U["SE_send(err)"]
        Phase2 --> U
        U --> V["se_log_error_chain()"]
        V --> V1["sys_data_connector_send(CONN_ID_LOGS)"]
        V --> V2["ESP_LOGE() (Serial Mirror)"]
        U --> W["SE_send_error_raw()"]
        W --> W1["enc_sys_errors_encode_chain()"]
        W1 --> W2["sys_data_connector_send(CONN_ID_ERRORS)"]
        W2 --> W3["ble_provider_send() -> sys_ble_char_send()"]
    end

    subgraph Phase4["Phase 4: Cleanup"]
        U --> X["SE_release(err)"]
    end
```

### 2. Detailed Sequence & Call Tree Diagram

```mermaid
sequenceDiagram
    autonumber
    actor Caller as Caller Task (e.g. AP33772S Driver)
    participant Core as sys_error.c
    participant Handler as sys_error_handler.c
    participant SysDev as sys_device.c
    participant Policy as runit_device_error_policy.h
    participant Actions as sys_actions.c
    participant VM as vm_exec.c
    participant Domain as sys_<domain>_handle_fault
    participant Log as sys_error_log.c
    participant Conn as sys_data_connector.c
    participant BLE as sys_ble_stack.c

    %% 1. Raising
    Note over Caller, Core: Step 1: Error Generation & Chaining
    Caller->>Core: SE_create(owner, tag, payload_size)
    Caller->>Core: SE_wrap(outer_err, cause_err)
    Caller->>Handler: SE_push_to_handler(err)

    %% 2. Collection & Attribution
    Note over Handler, SysDev: Step 2: Inspection & Device Attribution
    Handler->>Core: SE_collect_chain(err, nodes[], &complete)
    loop For each node in chain
        Handler->>Handler: device_id_of(node, &dev_id)
        alt Node is attributed to a device
            Handler->>SysDev: sys_device_report_error_with_level(dev_id, level, node)
            SysDev->>SysDev: sys_device_get_by_id(dev_id)
            alt dev->importance == SYS_DEV_IMPORTANCE_NONE
                SysDev-->>Handler: NULL (Ignored test device)
            else Active Device
                SysDev->>SysDev: Clamp level to dev->importance (if non-critical)
                SysDev->>SysDev: action_id = dev->actions[level]
                SysDev->>Policy: sys_device_app_error_policy(dev_id, level, action_id, node)

                opt action_id != 0
                    Policy->>Actions: runit_invoke_action(action_id) -> sys_actions_invoke(scope, id)
                end

                opt level == SYS_DEV_ERR_CRITICAL
                    Policy->>Policy: runit_system_fault(dev_id, node)
                    Policy->>VM: vm_exec_fault_latch(dev_id, owner, tag)
                    Policy->>VM: vm_exec_stop()
                    Policy->>SysDev: sys_device_freeze_all()
                end
                Policy-->>SysDev: policy_response
                SysDev-->>Handler: response
            end
        end

        %% 3. Domain Dispatch
        Note over Handler, Domain: Step 3: Domain Specific Fault Handling
        Handler->>Handler: dispatch_domain(node, err)
        Handler->>Domain: sys_<domain>_handle_fault(node, err)
        Domain-->>Handler: domain_response

        opt level == CRITICAL and system not yet handled
            Handler->>Policy: sys_system_handle_fault(node, err)
        end
    end

    %% 4. Baseline Logging & Telemetry
    Note over Handler, BLE: Step 4: Baseline Invariant (All Error Chains)
    Handler->>Log: SE_send(err)

    rect rgb(240, 245, 255)
        Note over Log, Conn: 4a. Text Expansion & Logging
        Log->>Log: se_log_error_chain(err)
        loop For each node
            Log->>Core: SE_describe_payload(tag, payload, desc)
            Log->>Conn: sys_data_connector_send(CONN_ID_LOGS, line, len)
            opt mirror_on_serial == true
                Log->>Log: ESP_LOGE(TAG, "%s", line)
            end
        end
    end

    rect rgb(240, 255, 245)
        Note over Log, BLE: 4b. Wire Encoding & Raw Transport
        Log->>Log: SE_send_error_raw(err)
        Log->>Core: enc_sys_errors_encode_chain(err, packet, max_len)
        Log->>Conn: sys_data_connector_send(CONN_ID_ERRORS, packet, len)
        Conn->>BLE: ble_provider_send(arg, frame, len)
        BLE->>BLE: sys_ble_char_send()
    end

    %% 5. Release
    Note over Handler, Core: Step 5: Allocator Reclamation
    Handler->>Core: SE_release(err)
```

### 3. Component Symbol & Call Reference Map

| Component / Step | Source File | Key Symbols / Functions |
| :--- | :--- | :--- |
| **Error Allocation & Chaining** | `sys_error.c` | `SE_create()`, `SE_wrap()`, `SE_collect_chain()`, `SE_release()` |
| **Handler & Central Dispatch** | `sys_error_handler.c` | `SE_push_to_handler()`, `device_id_of()`, `dispatch_domain()` |
| **Device Error Routing** | `components/system/sys_device/sys_device.c` | `sys_device_report_error_with_level()`, `sys_device_report_error()`, `sys_device_freeze_all()` |
| **App Coordinator Policy** | `components/runit/runit_device_error_policy.h` | `sys_device_app_error_policy()`, `runit_system_fault()`, `runit_invoke_action()` |
| **Action Execution** | `components/system/sys_actions/sys_actions.c` | `sys_actions_invoke()` |
| **VM Fault Containment** | `components/VM/core/exec/vm_exec.c` | `vm_exec_fault_latch()`, `vm_exec_stop()`, `sys_vm_handle_fault()` |
| **Logging & Wire Telemetry** | `sys_error_log.c` | `SE_send()`, `se_log_error_chain()`, `SE_send_error_raw()` |
| **Bus Switchboard** | `components/system/sys_data_connector/sys_data_connector.c` | `sys_data_connector_send()` |

