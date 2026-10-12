# vm_core — the program runtime (for agents)

Component `components/vm/vm_core/` (ported 2026-10-12 from `legacy/components/VM/core`). The VM runs a program that the app compiles off-device and uploads: objects (typed data), accessors (paths to values), blocks (behaviour), executed as scan passes. This component is the **core** only: store, objects, accessors, the block layer, the supervisor, the loader, telemetry and retained values. The block types (the palette) are the separate component `vm_blocks` (VM_BLOCKS.MD): each block registers itself, and the core runs whatever is registered.

The design documents of the legacy VM still describe the model and are the source for what is unchanged: `legacy/components/VM/VM.MD` (object model, accessors, arena, loader, execution), `VM_EXEC.MD` (execution model and why), `VM_PERF.MD` (hot-path measurements, history), `core/obj/VM_OBJ_ACCESS.MD` and `VM_OBJ_CONTRACTS.md`. This file lists only what differs in the new tree.

## Layout

```
vm_core/
  errors/    vm_err.h (the tags, ERR_MOD_VM = 15, object ID constants, debug names), vm_errors.c/h (cold-path builders)
  store/     arena + the three registries
  obj/       objects, accessors, dynamic objects, copy / clone / link
  block/     block layout and API (vm_block.h), build + verify, helpers, vm_block_ids.h (the type numbers),
             vm_blocks.h/.c (the palette registry: VM_BLOCK_TYPE_DEFINE, lookups)
  exec/      supervisor (vm_exec.c), program clock, pass statistics, events, runtime overrides
  loader/    load state machine, vm_wire.h (every wire record)
  sub/       telemetry subscriptions (vm_sub.c)
  retain/    retained values (vm_retain.c)
  vm_router.c/.h   the remote interface (ROUTER_KEY_VM = 0x0B), vm_init, vm_stop_on_fault
```

Includes are by bare filename; every folder is on the include path (`CMakeLists.txt`). The component builds at `-O2` (hot path) and is linked `WHOLE_ARCHIVE` (tag registration).

## Remote (`ROUTER_KEY_VM` = 0x0B, `vm_router.h`)

Replaces the class-`0x04` decoder (`dec_vm_loader.h`). Same records, now router ops; ops never renumber.

| Op | Payload | Effect |
|---|---|---|
| `0x01` RESET | – | Unload everything; leaves the VM stopped |
| `0x02` OPEN | `vm_wire_open_t` | Replace the program with an empty one of this size (fail closed) |
| `0x03` ADD_OBJECTS | `u8 n`, n × (`vm_wire_obj_t`, name) | Create + bind objects |
| `0x04` SET_DATA | `u8 n`, n × (`vm_wire_data_t`, data) | Stopped: load values and PTR links. Running: a runtime write, applied at the next pass start |
| `0x05` ADD_ACCESSORS | `u8 n`, n × (`vm_wire_acc_t`, steps) | Create + bind + pre-resolve |
| `0x06` ADD_BLOCK | `vm_wire_block_t`, pins, state | One block per frame, in execution order |
| `0x07` SUBSCRIBE | `u8 n`, n × `vm_wire_sub_t` | Telemetry for those objects; 0 clears |
| `0x08` EXEC | `vm_wire_exec_t` | `vm_exec_command_e` (scan, once, block, next, rewind, normal, pause, resume, reset, ack fault, forget retained) |
| `0x09` SPEED | `vm_wire_speed_t` | Slow motion (debugging aid) |

Behaviour (load order, fail-closed abort, what a running VM refuses, runtime writes through overrides) is the legacy `wire.md`, unchanged; only the packet bytes `0x40..0x49` became these op numbers and the `[seq][class][packet]` header became `[seq][flags][0x0B][op]`.

### Telemetry is the same command

A telemetry frame sent by `vm_sub.c` on the telemetry connector is exactly a command of this router, without `seq` / `flags`:

```
values        [0x0B][0x04 SET_DATA][u8 count] count × (vm_wire_data_t, data)
heap objects  [0x0B][0x03 ADD_OBJECTS][u8 count] count × (vm_wire_obj_t, name)
```

So the board writes values out in the format the app writes them in: a telemetry frame can be sent back as a command (it sets the same values), recorded into a record (SYS_ACTIONS.MD), and the app parses both directions with one decoder. The record structs are shared (`vm_wire.h`), the key and ops are the same constants (`ROUTER_KEY_VM`, `VM_OP_*`). Sending behaviour is unchanged: only objects whose value changed since last sent (value hash), everything once after a subscribe, large objects split by `start_idx`, a heap object described before any value that refers to it.

## Changes from legacy

| Part | Change |
|---|---|
| Errors | New module `ERR_MOD_VM` (15): one `ERR_VM_MAP` in `errors/vm_err.h`, tag numbers new, severities kept. No owners, no `*_OWNED` macros. Added `ERR_VM_NO_MEM`, `ERR_VM_BAD_ARG`, `ERR_VM_RANGE`; the shared legacy tags (`ERR_BASE_NO_MEM`, `ERR_NULL_PTR`, `ERR_INVALID_VAL_UI32`) map to those. `ERR_VM_NO_ACTION_EXECUTOR` is gone. `VM_RAISE(tag, …)` = `err_report(ERR_NEW(…))` for an error that ends where it is raised |
| Fault latch | `vm_exec_fault_latch(u16 device, u32 root_tag)`; the owner hook `sys_vm_handle_fault` is gone. A critical device error reaches the VM through `sys_device_set_vm_stop` (SYS_DEVICE.MD): the app registers `vm_stop_on_fault`, which latches and stops the VM before the devices are suspended |
| Actions | `vm_exec_request_action(u32 id)` calls `sys_actions_request` directly (no scope, no registered function); the VM sits above sys_actions |
| Events | One wildcard `sys_event` subscription (`vm_event_init`, queued) fills the per-pass queue. `ON_EVENT` matches the per-pass snapshot by source, event and channel with `sys_event_matches` |
| Telemetry | `sys_connector_send(SYS_CONNECTOR_TELEMETRY)`; frame header `[ROUTER_KEY_VM][op][count]` (above) |
| Retained values | One NVS blob `vm/retain` in the default partition (own helpers in `vm_retain.c`); `sys_settings` is gone. Same rules: kept by name, saved every `CONFIG_VM_RETAIN_PERIOD_S` off the VM task, restored on the first start after a load, `EXEC` forget clears it |
| Debug output | `DBG(...)` calls became `ESP_LOGD` |
| Palette | A registry of pointers, `g_vm_block_types[VM_BLOCK_TYPES_MAX]`, filled before `app_main` by `VM_BLOCK_TYPE_DEFINE` in each block file (like `SYS_DEVICE_TYPE_DEFINE`); no generated id header or table. The numbers are in `block/vm_block_ids.h`. The blocks are in `vm_blocks` |

## Boot and safe state (RUNIT.MD)

Boot step `vm` (`vm_init`: event subscription, telemetry, retained values, the supervisor task) runs after the router is up. `runit_enter_safe_state` stops the VM and saves the retained values before it suspends the devices.

## Open

- **Palette:** the logic blocks are ported (VM_BLOCKS.MD). Device blocks (u16 `device_id`) are one per contract now (VM_BLOCKS.MD "Device blocks"; not run on a board), every contract with signal-like ops has one (also POWER_VREG, POWER_SUPPLY, POWER_USB_PD, LIFECYCLE); the devices with own ops that a program needs have a block (servo, DRV8962, TPS55289: `device-custom-block/`), make "run once at start" an explicit option (legacy G-12). 
- **Generators:** `data-structures/auto-annotations/vm/` and the published `vm-program.generated.json` / block descriptors still read the legacy paths and `HEADER_packet_vm_*` names (the annotations in `vm_wire.h` now name `VM_OP_*`). Re-point them before the app is rebuilt.
- **Program storage: done in `sys_project` (SYS_PROJECT.MD)**, the plan below is what it implements (chunks under class PROJECT, a list record of `RUN` frames); not run on a board. Original note:  a program can be far larger than a record (legacy allowed 200 KB). Plan: a program is a series of records of at most 4 KB under a `PROGRAM` class, run by a container record of `RUN` frames (SYS_ACTIONS.MD). Not implemented; nothing stores a program yet.
- **Retained values as a record:** a snapshot as `SET_DATA` frames in a record would make readback symmetric with the rest; not done (a plain NVS blob for now).
- **`vm_object` header on the wire** is an explicit 4-byte layout now (`vm_wire_head_t`, `vm_wire_head_pack` / `_unpack` in `vm_wire.h`): u16 payload size, type | name length << 4, flags (mutable 0, upd_resetable 2, tagged 3, retentive 4, dynamic 5, usr_protected 6; bit 1 and 7 read as 0). The bytes are the ones the GCC bitfield gave, so nothing the app already sends changes; only the board's own flags (tagged, dynamic) are no longer taken from the wire, and the runtime `upd` is no longer sent.
- **Tests:** `.claude/skills/runit-esp/scripts/vm_suite.py` (`--port COM9`, runs on the PCB; `--only`, `--list`, `--skip-hw`), written 2026-10-12 against this code: the wire (every op truncated and malformed, fail closed, ids), objects, accessors, supervisor modes (scan / ONCE, block / NEXT, pause, restart, speed), runtime writes and the override ring, telemetry (limits, split objects, trees, a frame as a command), retained values, loads and limits, and every block (VM_BLOCKS.MD). 945/945 on the PCB (2026-10-12, ~4.5 min). `vm_core_tests.py` is the older, smaller suite. Not covered: a latched device fault (`vm_stop_on_fault`), the heap-object limit (`CONFIG_VM_DYN_MAX`), BLE as the telemetry transport.
- **Found by the suite and fixed (2026-10-12):** (1) Object, accessor and block id `0xFFFF` from the wire was accepted: it is the "allocate without binding" value of `vm_store_alloc`, so the item was built but never registered. The loader now answers `ERR_VM_REG_OOB`. (2) An accessor could take a reference step to itself (it is bound before its steps are read); refused now. (3) A telemetry object larger than the connector queue (`CONFIG_SYS_CONNECTOR_QUEUE_SIZE` 4096) never arrived whole: the queue dropped what did not fit and every sample started again at the first byte. A tracked object now keeps `resume` (the first element not delivered) and the next sample continues there; if the value changed meanwhile it starts over (`track_t` is 16 bytes now, 24 per tracked object with the table's slack). (4) FOR with a NaN or infinite start / end / step now reports `ERR_VM_FOR_BAD_LOOP` (reason not finite), once until a loop has run to its end again.
- **Still open:** (5) Initial values uploaded with the program are not fresh, so a block triggered by its inputs (EXPR, SET, ...) does nothing until an input is written at run time; documented behaviour, but the app must write the inputs (or a gate) once after the start. (6) A request now and then gets no answer on the USB-Serial-JTAG link (twice in ~1500 requests, not repeatable); no cause found.
- **Start with no program:** `EXEC` normal on an empty VM succeeds (`vm_loader_verify_complete` checks only an open program), as in legacy; it runs zero blocks. Decide whether the app should see an error instead.
