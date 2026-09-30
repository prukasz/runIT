# VM — entry point

Read this first for any work under `components/VM/`, on the class `0x04` decoder (`codecs/decoders/dec_vm_loader.h`), or on anything the app needs in order to build and upload programs.

The component docs hold the design; this folder holds routing, recipes, and what is known to be wrong or missing.

| File | Read when |
|---|---|
| [blocks.md](blocks.md) | Adding or changing a block type; the palette as it stands |
| [wire.md](wire.md) | Upload protocol (class `0x04`), runtime writes, telemetry, execution control, what reaches the app's JSON |
| [findings.md](findings.md) | Open gaps and decisions (`0x04` packets in JSON, tests, retention) |

Component docs (source of truth; brought up to date 2026-09-23):

| Doc | Covers |
|---|---|
| `components/VM/VM.MD` | Object model, accessors, arena and registries, loader and wire format, execution, palette (15 blocks), RAM budget |
| `components/VM/VM_PERF.MD` | Hot-path measurements and decisions — history (the benchmark harness was removed) |
| `components/VM/VM_EXEC.MD` | The execution model and *why*; its status table says what is built and what is still design |
| `components/VM/core/obj/VM_OBJ_ACCESS.MD` | Object / accessor API catalog with examples |
| `components/VM/core/obj/VM_OBJ_CONTRACTS.md` | Lifetime, ownership, schema matching, mutation and freshness rules |

## 1. The model in one page

A program is compiled **off-device by the app** into a flat description and uploaded over class `0x04`. The device rebuilds it into three id spaces over one per-program arena:

- **Objects**: typed data (`vm_obj_t`: 4-byte header + payload + optional tag). Types: `U8 U32 I32 F B STR PTR`. There is no struct type: nesting is a `PTR` array of child handles.
- **Accessors**: a root object id plus a chain of index steps (`LITERAL`, `REF` = index read live from another accessor, `NAME` = child by tag). Every input pin and enable source is an accessor; pins reading the same thing share one.
- **Blocks**: behaviour. `[cfg 16 B][inputs: accessor*][outputs: obj handle][enable list: accessor*][custom_data]`. The type byte indexes the palette `g_vm_block_types[]` (body + shape + state size per type).

Execution is a **scan pass**, looped on a task on core 1 (one tick yield per pass, 10 ms floor):

```
latch clock + drain events + apply runtime overrides
→ walk blocks 0..n-1 in upload (= topological) order; span owners (FOR) run their range and the walk jumps past it
→ sample telemetry subscriptions → clear `upd` on resettable objects
```

Rules that everything else follows:

- **The client sorts, the device never does.** Block id = execution order.
- **Every block is called every pass.** The supervisor gates nothing; a block decides whether it acts (`vm_block_triggered`, `vm_block_is_enabled`).
- **Not acting = keep outputs, drop ENO quietly.** ENO true is loud (sets `upd`), false is quiet. Routers (IF/SWITCH) are the one exception: their outputs are flow, so they clear them (quietly) too.
- **Freshness (`upd`) is cleared by the supervisor at end of pass**, never by a consumer (fan-out would break).
- **`cfg.on_error`**: `STOP` drops ENO after a failed body, `CONTINUE` leaves the flow. A failure is the block's own per-call bit (`VM_BLK_RT_FAULT`); errors go out through `BLOCK_CALL` / `vm_block_report_error`, tagged with the block.
- **No allocation after load**, except CLONE (heap `vm_obj_dyn`, refcounted, only on schema change).
- **Everything from the wire is untrusted**: ids, counts and lengths are checked before storage; an id that was never bound reads NULL.
- **Events** (`sys_event`, route slot `CONFIG_SYS_EVENT_ROUTE_VM`) live exactly one pass; `ON_EVENT` blocks match them with `vm_event_count()` / `vm_event_at()`.
- **Retained values** (`retentive` objects) are kept by name: saved every 60 s off the VM task, restored on the first start after a load (VM.MD "Retained values").
- **Actions are requested, never run in a pass**: `ACTION` → `vm_exec_request_action()` → the registered request function (runit: `sys_actions_request()`, a queue on the actions task).

## 2. Code map

```
components/VM/
  blocks/            palette: vm_blocks.h (type ids, lookups, vm_block_verify), vm_blocks_table.c (g_vm_block_types), one header per block (body, state, VM_BLOCK_TYPE_*, //#vm-block)
  core/store/        arena + three registries (vm_store_*)
  core/obj/          objects, accessors, dynamic objects, copy/clone/link
  core/block/        block layout and API (vm_block.h), build + verify (vm_block_build.c), helpers
  core/exec/         supervisor (vm_exec.c), events, runtime overrides
  core/loader/       load state machine + wire-facing validation
  core/sub/          telemetry subscriptions (vm_sub.c)
  core/retain/       retained values (vm_retain.c, retain task, sys_settings record vm_retain)
  core/errors/       vm_errors.c/h (block error builders)
  errors/            sys_error_vm.h: owners 0xA900–0xA908, ~100 ERR_VM_* tags, SE_*_OWNED macros
codecs/decoders/dec_vm_loader.h   class 0x04 decoder (framing only)
```

- Includes are by **bare filename**; each `core/*` folder is on the include path (VM `CMakeLists.txt`).
- `blocks/` depends on `core/`, never the reverse: the supervisor dispatches through the table's *declaration* and knows no block.
- The component builds at **`-O2`** regardless of the project's `-Og` (hot path), with **`-Woverride-init`** so two block types claiming one id warn.
- Kconfig menu "VM Configuration": task prio/core, span depth, watchdog, event depth, override buffer, pin limits (16/16/16), pool ceiling (128 kB), accessor/copy/dyn depths, subscription limits.

## 3. Wiring from runit

| What | Where |
|---|---|
| Class `0x04` decoder | `runit_register_decoders()` |
| Event route `CONFIG_SYS_EVENT_ROUTE_VM` → `vm_event_route` | `runit_error_wiring_init()` |
| Domain hook `OWNER_VM_BASE` → `sys_vm_handle_fault` | `runit_error_wiring_init()` |
| Critical device fault → `vm_exec_fault_latch()` + `vm_exec_stop()` | `runit_device_policy` (`runit_error_policy.c`) |
| Safe state: `vm_exec_stop()` + `sys_device_suspend_all()` | `runit_enter_safe_state()` |
| Action requests → `sys_actions_request` (`vm_exec_register_action_request`) | `runit_error_wiring_init()` |
| `vm_sub_init`, `vm_retain_init` (boot steps), `vm_exec_start` (runtime step) | `runit_start()` |
| Save retained values after stopping the VM | `runit_enter_safe_state()` |

## 4. Rules for VM work

- Follow [../conventions.md](../conventions.md) and [../errors.md](../errors.md). VM-specific: header-only code pulled into other translation units uses the `SE_*_OWNED` macros from `sys_error_vm.h` (explicit owner), e.g. `VM_BLK_ERR_NEW`.
- Keep the hot path allocation-free and call-free where it is now (`vm_resolve_fast`, cached accessors). VM_PERF.MD records the measured trade-offs (historical numbers); re-measure rather than guess before changing them.
- A block's private-state struct is **wire format**: fixed size, `_Static_assert`ed, explicit padding, 4- (or 8-) byte aligned, runtime-only fields zero on the wire.
- When a block or packet changes, update the component doc and the app-facing descriptors in the same change (see wire.md "What reaches the app").
