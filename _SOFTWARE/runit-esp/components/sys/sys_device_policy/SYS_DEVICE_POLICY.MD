# sys_device_policy — device error policy and fault response task (for agents)

Folder `components/sys/sys_device_policy/`. Decides how the board reacts to errors reported on devices without adding locking or response logic to [`sys_device`](../sys_device/SYS_DEVICE.MD) or running ops on the caller's task.

## Files

| File | Role |
|---|---|
| `sys_device_policy.h` | Importance levels, `sys_device_error_policy_t`, built-in responses, fault stages, callback types, API (`init`, `set_vm_stop`, `set_action_runner`, `handle`, `get`, `set`) |
| `sys_device_policy.c` | Slot policy table, `CREATED`/`REMOVED` inline listener, queue ring buffer, dedicated policy task |
| `sys_device_policy_id.h` | `MODULE_ID_DECLARE(MODULE_ID_POLICY, 0x..)` |
| `router/sys_device_policy_router.h` | `sys_device_policy_key_t`, `sys_device_policy_entry_t`, `POLICY_OPERATIONS` (`OP_POLICY`) |
| `router/sys_device_policy_router.c` | `ROUTER_READ`/`ROUTER_WRITE` handlers and `ROUTER_MODULE_REGISTER(MODULE_ID_POLICY, sys_device_policy_router)` |
| `Kconfig` | Queue size, task priority and stack size |
| `CMakeLists.txt` | Requires `errors`, `router`; privately `sys_device`, `sys_event`, `router_queue` (the `ERR_REMOTE` tag), `utils`, `freertos`, `log`; linked `WHOLE_ARCHIVE` |
| `AGENTS.md` | Rules for changing this folder |

## Model

Each device slot (0 .. `SYS_DEVICE_COUNT - 1`) holds:

```c
typedef struct __attribute__((packed)) {
  uint8_t importance;                       // sys_device_importance_e (default LOW = 1)
  uint32_t actions[SYS_DEVICE_ERR_LEVELS];  // actions[sev - 1] for LOW..CRITICAL: action ID, response, or 0
} sys_device_error_policy_t;  // 17 B
```

- **Importance** (`sys_device_importance_e`):
  - `SYS_DEVICE_IMPORTANCE_NONE` (0): stops the chain walk at that device (ignores its errors and every deeper cause).
  - `SYS_DEVICE_IMPORTANCE_LOW` (1, default): admits `CRITICAL` only.
  - `SYS_DEVICE_IMPORTANCE_MEDIUM` (2): admits `HIGH` and `CRITICAL`.
  - `SYS_DEVICE_IMPORTANCE_HIGH` (3): admits `MEDIUM`, `HIGH`, `CRITICAL`.
  - `SYS_DEVICE_IMPORTANCE_CRITICAL` (4): admits `LOW`, `MEDIUM`, `HIGH`, `CRITICAL`.
- **Admission rule**: `sev == ERR_SEV_CRITICAL` is always admitted (even if `found == false`, e.g. a failed `CREATE` where `vm_stop` + global `SUSPEND` still run without a slot action); otherwise admitted iff `sev + importance > ERR_SEV_CRITICAL`.
- **Actions** (`SYS_DEVICE_ERR_LEVELS` = 4): `actions[sev - 1]` for `ERR_SEV_LOW`..`ERR_SEV_CRITICAL` is `0` (no action), a built-in response (`SYS_DEVICE_RESPONSE_SUSPEND = 0xFF000001u`, `SYS_DEVICE_RESPONSE_RESET = 0xFF000002u`), or an application action ID passed to `action_runner`.

## Lifecycle and execution flow

1. **Start (a constructor, before `app_main`)**: creates the policy task (`sys_device_policy_task_h`) and ring buffer (`sys_device_policy_ring`), and subscribes `SYS_EVENT_RUN_INLINE` to `SYS_DEVICE_EV_CREATED` and `SYS_DEVICE_EV_REMOVED`.
2. **Registry tracking**: guarded by a short critical section (`s_mux`), never taking registry locks:
   - `CREATED` resets the slot to `{importance = SYS_DEVICE_IMPORTANCE_LOW, actions = {0}}`, `present = true`, `gen = SYS_EVENT_DATA(event, SYS_DEVICE_EV_CREATED)->gen`.
   - `REMOVED` clears `present = false` only when `SYS_EVENT_DATA(event, SYS_DEVICE_EV_REMOVED)->gen == current.gen`, ignoring stale `REMOVED` events that arrive after a newer `CREATED`.
3. **Reporting (`sys_device_policy_handle`)**: called on the reporting task (from the app's error sink):
   - immediately returns `NULL` if `chain == NULL`, the chain contains `ERR_DEV_FAULT_RESPONSE`, or the current FreeRTOS task is `sys_device_policy_task_h` (prevents response recursion);
   - performs a dry walk of `chain` to see if any node is admitted; if none, returns `NULL` without queuing;
   - serializes `chain` via `err_record` into a stack buffer of `ERR_RECORD_MAX` bytes (err_record.h) and pushes it to `sys_device_policy_ring` (zero blocking; returns `ERR_DEV_FAULT_RESPONSE` with `SYS_DEVICE_FAULT_STAGE_QUEUE` if serialization fails or the ring is full).
4. **Policy task (`sys_device_policy_task_h`)**: pops one serialized record, parses it with `err_record_parse`, and walks from outermost node to root:
   - the walk ends at `ERR_REMOTE`: a remote device's error is `ERR_DEV_ID{local id}` over `ERR_REMOTE{board, seq}` over that board's chain, whose device IDs are not this board's (the severity from the device down still counts the whole chain);
   - first `ERR_DEV_ID` occurrence per device (`HANDLED_MAX` = 16 unique devices per chain), computing the sub-chain severity from that node down (`err_chain_sev`);
   - a `CRITICAL` node before any `ERR_DEV_ID` triggers `vm_stop(SYS_DEVICE_ID_NONE)` + `sys_device_sweep(CONTRACT_SYS_DEVICE_OP_SUSPEND)` once per chain;
   - on an admitted `CRITICAL` device node, runs `vm_stop(id)` + global `SUSPEND` sweep (`sys_device_sweep`) before running the slot's action;
   - runs that device's configured response (`SYS_DEVICE_RESPONSE_SUSPEND`, `SYS_DEVICE_RESPONSE_RESET`, or `action_runner(action, chain)`).
   - wraps any response failure in `ERR_DEV_FAULT_RESPONSE{id, sev, stage, action}` (`sys_device_fault_stage_e`: `VM_STOP=0`, `SUSPEND=1`, `ACTION=2`, `QUEUE=3`) and reports it via `err_report`.

## Router (`router/sys_device_policy_router.h`, key `MODULE_ID_POLICY`)

`MODULE_ID_POLICY` (`sys_device_policy_id.h`) registers `sys_device_policy_router` under its root key (`[MODULE_ID_POLICY][dir][0x01][args]`).

| Op | Dir | Args | Answer | Notes |
|---|---|---|---|---|
| `0x01 OP_POLICY` | R | `sys_device_policy_key_t { uint8_t id; }` (1 B) | `sys_device_policy_entry_t { uint8_t id; sys_device_error_policy_t policy; }` (18 B) | `ERR_DEV_NOT_FOUND` if slot absent |
| `0x01 OP_POLICY` | W | `sys_device_policy_entry_t` (18 B) | — | Validates `importance` and responses (`ERR_DEV_RANGE`, `ERR_DEV_BAD_ARG`, `ERR_DEV_NOT_FOUND`) |

## Kconfig

| Option | Default | Role |
|---|---|---|
| `CONFIG_SYS_DEVICE_POLICY_QUEUE_BYTES` | `1024` | Ring buffer capacity (bytes) for serialized error records |
| `CONFIG_SYS_DEVICE_POLICY_TASK_PRIO` | `14` | Policy response task priority |
| `CONFIG_SYS_DEVICE_POLICY_TASK_STACK` | `4096` | Policy response task stack size (bytes) |

## API

```c
void sys_device_policy_set_vm_stop(sys_device_vm_stop_f fn);
void sys_device_policy_set_action_runner(sys_device_action_runner_f fn);
MUST_USE err_h sys_device_policy_handle(err_h chain);
bool sys_device_policy_get(uint8_t id, sys_device_error_policy_t* out);
MUST_USE err_h sys_device_policy_set(uint8_t id, const sys_device_error_policy_t* p);
```

## Tests

No standalone host folder; end-to-end policy flows are planned around `sys_device_policy` queue/task isolation and generation checks (`SYS_DEVICE.MD`).
