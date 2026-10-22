# vm_core — the program runtime (for agents)

Component `components/vm/vm_core/`. The VM runs a program that the app compiles off-device and uploads: objects (typed data), accessors (paths to values), blocks (behaviour), executed as scan passes. This component is the **core** only: store, objects, accessors, the block layer, the supervisor, the loader, telemetry and retained values. The block types (the palette) are the separate component `vm_blocks` (VM_BLOCKS.MD): each block registers itself, and the core runs whatever is registered.

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
  router/    vm_router.c/.h, contract_vm.json: the remote interface (ROUTER_KEY_VM = 0x0B), vm_init, vm_stop_on_fault
```

Includes are by bare filename; every folder is on the include path (`CMakeLists.txt`). The component builds at `-O2` (hot path) and is linked `WHOLE_ARCHIVE` (tag registration).

## The model

A program lives in three ID spaces over one arena per program (`vm_store`; size and slot counts come with OPEN, `CONFIG_VM_STORE_MAX_POOL` caps it):

- **Objects** (`vm_obj.h`): a 4-byte head, a payload of one value or an array, an optional tag (name, up to 15 chars). Types `PTR U8 U32 I32 F B STR`. There is no struct type: a tree is a `PTR` array of child objects. Flags: mutable, retentive, usr_protected; the board's own: tagged, dynamic.
- **Accessors** (`vm_obj_access*.h`): a root object plus a chain of index steps: `LITERAL` (fixed position), `REF` (index read live from another accessor), `NAME` (child by tag). Every input pin and enable source is an accessor. At load the root must exist, and a fixed path (no steps, or one `LITERAL` on the root) is resolved once into the accessor's cache, or refused (`ERR_VM_ACCESSOR_UNKNOWN_ID` / `_OOB`): reading it is then a flag test and a copy (`vm_acc_resolve`). Any other path is walked when used (`vm_acc_walk`).
- **Blocks** (`vm_block.h`): `[cfg 16 B][inputs: accessor*][outputs: object][enables: accessor*][state]`. `block_type` indexes the palette (`g_vm_block_types[]`), which gives the body, the optional load check, the pin shape and the state size.
- **Heap objects** (`vm_obj_dyn`): trees built at run time (a CLONE's copy; a block building a tree with `vm_obj_dyn_new` / `vm_obj_dyn_adopt`). ID = `VM_OBJ_ID_DYN_BIT` | slot, at most `CONFIG_VM_DYN_MAX` live. A small header in front of the object holds its slot and its owner. Ownership is a tree: a heap object hangs in at most one PTR cell, and losing that cell frees it with everything below it; sharing means copying (CLONE). Linking a tree into a cell checks it once (no owner yet, not containing the cell, at most `CONFIG_VM_DYN_MAX_DEPTH` heap levels: `ERR_VM_OBJ_OWNERSHIP`). Nothing else allocates after the load.

**A pass** (the supervisor task, `vm_exec.c`, `CONFIG_VM_EXEC_TASK_CORE`): latch the program clock, take the events of this pass, apply the runtime writes (overrides), run blocks `0..n-1` in upload order (a span owner, FOR, runs its range and the walk jumps past it), sample telemetry, hand the retained values over. One tick yield between passes (times the slow-motion factor).

Rules everything else follows:

- **The client sorts, the board never does.** Block ID = execution order.
- **Every block is called every pass, and acts on every pass it is enabled** (`vm_block_is_enabled`): EN → do it. There is no hidden "new data" state: a value is a level, read as it is now. An event is a value too: a pulse that is on for one pass (EDGE, PERIODIC, ON_EVENT), wired to an enable; LATCH holds one until a later block acts on it. A block may itself skip work it already did (CLONE refills its copy in place while the shape stays the same).
- **Not acting = outputs kept, ENO off.** The routers (IF, SWITCH) write every output, 1 on the branch taken and 0 on the others: their outputs are enable gates.
- **`cfg.on_error`**: `STOP` drops ENO after a failed call (everything below it skips), `CONTINUE` leaves the flow; both report the error. Outputs are never retracted. A failure is the block's per-call bit `VM_BLK_RT_FAULT`; errors go out through `BLOCK_CALL` / `vm_block_report_error`, tagged with the block.
- **Everything from the wire is untrusted**: IDs, counts and lengths are checked before anything is stored. An ID that was never bound reads NULL; ID `0xFFFF` is refused (`ERR_VM_REG_OOB`), and an accessor cannot take a reference step to itself.
- **Block watchdog**: a block that runs longer than `CONFIG_VM_EXEC_BLOCK_WD_MS` is reported once.
- **Run modes** (`vm_run_mode_e`, set by `EXEC`): normal (passes back to back), scan (one pass per `ONCE`), block (one block per `NEXT`), pause / resume, reset to start. In block mode the supervisor parks before each block, and at each stop it sends what the block before changed (the telemetry sample, once per stop) instead of waiting for the end of the pass; `STATUS` says which block the next `NEXT` runs (`next_block`: the one it is parked before, 0 between two passes). Time spent not running passes does not count for the program clock (`vm_clock.h`); so that stepping does not stop the timing blocks, each pass asked for (scan `ONCE`, a whole pass of `NEXT`s) adds `step_ms` of program time (SPEED, default 10 ms; 0 stands time still).

## Remote (`ROUTER_KEY_VM` = 0x0B, `vm_router.h`)

A request is `[0x0B][dir][op][payload]`: `dir` (`cmd_dir_e`, read 0 / write 1) picks the side of the op (a side the op does not have: `ERR_CMD_DIRECTION`). Ops never renumber.

| Op | Dir | Payload | Effect |
|---|---|---|---|
| `0x01` RESET | W | – | Unload everything; leaves the VM stopped |
| `0x02` OPEN | W | `vm_wire_open_t` | Replace the program with an empty one of this size (fail closed) |
| `0x03` ADD_OBJECTS | W | `u8 n`, n × (`vm_wire_obj_t`, name) | Create + bind objects |
| `0x04` DATA | W | `u8 n`, n × (`vm_wire_data_t`, data) | Stopped: load values and PTR links. Running: a runtime write, applied at the next pass start |
| `0x05` ADD_ACCESSORS | W | `u8 n`, n × (`vm_wire_acc_t`, steps) | Create + bind + pre-resolve |
| `0x06` ADD_BLOCK | W | `vm_wire_block_t`, pins, state | One block per frame, in execution order |
| `0x07` SUBSCRIBE | W | `u8 n`, n × `vm_wire_sub_t` | Telemetry for those objects; 0 clears |
| `0x08` EXEC | W | `vm_wire_exec_t` | `vm_exec_command_e` (scan, once, block, next, rewind, normal, pause, resume, reset, ack fault, forget retained) |
| `0x09` SPEED | W | `vm_wire_speed_t` | Slow motion (debugging aid): `factor`, and the `step_ms` of stepping (a 2-byte body is the factor alone) |
| `0x0A` STATUS | R | – | Answers `vm_wire_status_t`: run mode (`vm_run_mode_e`), load state, `ready` (loaded and complete: EXEC can start it), the latched fault (device and root tag, meaningful only while latched), slow-down, pass count, last pass time, arena bytes used, `next_block` (the block the next `NEXT` runs in block mode, `0xFFFF` otherwise). At any time |

**Load order**: RESET or OPEN, ADD_OBJECTS, DATA (initial values, PTR links), ADD_ACCESSORS, ADD_BLOCK one by one, then EXEC. The first start checks the program is complete (`vm_loader_verify_complete`).

**Fail closed**: any failing load op (OPEN to ADD_BLOCK, and the first start) discards the whole program (`vm_loader_abort`): the cause comes back wrapped in `ERR_VM_LOAD_ABORTED {packet = the op}` and the upload restarts at OPEN. Nothing is rolled back piecemeal.

**While running**: ADD_OBJECTS, ADD_ACCESSORS and ADD_BLOCK are refused and leave the program as it is; DATA is a runtime write (`vm_override`, a ring of `CONFIG_VM_OVERRIDE_BUF_SIZE` bytes) applied at the next pass start, refused for a `usr_protected` object; OPEN replaces the running program.

The object head on the wire is an explicit 4-byte layout (`vm_wire_head_t`, `vm_wire_head_pack` / `_unpack`): u16 payload size, type | name length << 4, flags `VM_OBJ_F_*` (mutable 0, tagged 3, retentive 4, dynamic 5, usr_protected 6; bits 1, 2 and 7 sent as 0, ignored when read). Only mutable, retentive and usr_protected are taken from the wire; tagged and dynamic are the board's.

### Telemetry is the same command

A telemetry frame sent by `vm_sub.c` on the telemetry connector (`SYS_CONNECTOR_TELEMETRY`) is a write of this router without `seq` / `flags` and without the direction byte (a stream frame has no side):

```
values        [0x0B][0x04 DATA][u8 count] count × (vm_wire_data_t, data)
heap objects  [0x0B][0x03 ADD_OBJECTS][u8 count] count × (vm_wire_obj_t, name)
```

So the board writes values out in the format the app writes them in: with `0x01` (write) after the key a telemetry frame is a command (it sets the same values) and can be stored in a record (SYS_ACTIONS.MD), and the app parses both directions with one decoder. The record structs are shared (`vm_wire.h`), the key and ops are the same constants (`ROUTER_KEY_VM`, `VM_OP_*`).

Sending: only objects whose value changed since last sent (value hash), everything once after a subscribe; sampled at the end of every pass, and in block mode also at every stop before a block (`block_gate` in `vm_exec.c`, once per stop), so a stepped program shows what each block did; an object larger than a frame is split by `start_idx`, and one larger than the connector queue continues where the last sample stopped (`resume`; it starts over if the value changed meanwhile). A heap object is described before any value that refers to it. Limits: `CONFIG_VM_SUB_MAX_SUBSCRIBERS`, `CONFIG_VM_SUB_MAX_TRACKED` (subscribed objects plus everything reachable through their PTR children; past it `ERR_VM_SUB_TRACK_FULL`), `CONFIG_VM_SUB_MAX_FRAME_LEN`.

## Integration

| Part | How |
|---|---|
| Errors | Module `ERR_MOD_VM` (15): one `ERR_VM_MAP` in `errors/vm_err.h`. `VM_RAISE(tag, …)` = `err_report(ERR_NEW(…))` for an error that ends where it is raised |
| Fault latch | `vm_exec_fault_latch(u16 device, u32 root_tag)`. A critical device error reaches the VM through `sys_device_set_vm_stop` (SYS_DEVICE.MD): `runit` registers `vm_stop_on_fault`, which latches and stops the VM before the devices are suspended; `EXEC` ACK_FAULT clears the latch |
| Actions | `vm_exec_request_action(u32 id)` calls `sys_actions_request` (queued, run on a router task, never inside a pass); the VM sits above sys_actions |
| Events | One wildcard `sys_event` subscription (`vm_event_init`, queued, `CONFIG_VM_EVENT_DEPTH`) fills the per-pass queue. An event lives exactly one pass; `ON_EVENT` matches the snapshot by source, event and channel with `sys_event_matches`. A full queue drops the event; each pass reports the drops since the last one as `ERR_VM_EVENT_OVERFLOW` (count and the last source) |
| Telemetry | `sys_connector_send(SYS_CONNECTOR_TELEMETRY)`; frame header `[ROUTER_KEY_VM][op][count]` (above) |
| Retained values | One NVS blob `vm/retain` in the default partition (`vm_retain.c`). Kept by name (a retentive object must be named), type and size; saved every `CONFIG_VM_RETAIN_PERIOD_S` off the VM task, on teardown (RESET, OPEN) and in the safe state; restored on the first start after a load; `EXEC` RETAIN_CLEAR forgets them. A program retaining more than `CONFIG_VM_RETAIN_MAX_BYTES` is refused at load |
| Palette | A registry of pointers, `g_vm_block_types[VM_BLOCK_TYPES_MAX]`, filled before `app_main` by `VM_BLOCK_TYPE_DEFINE` in each block file (like `SYS_DEVICE_TYPE_REGISTER`). The numbers are in `block/vm_block_ids.h`. The blocks are in `vm_blocks` |

## Boot and safe state (RUNIT.MD)

Boot step `vm` (`vm_init`: event subscription, telemetry, retained values, the supervisor task) runs after the router is up. `runit_enter_safe_state` stops the VM and saves the retained values before it suspends the devices.

## Open

- **Palette:** blocks for the own operations of the other devices (VM_BLOCKS.MD "Not yet written"); "run once at start" as an explicit block option.
- **A message block (JSON in, a heap tree out):** not written. The core is ready for it: the block builds the tree with `vm_obj_dyn_new` / `vm_obj_dyn_adopt` and links the root into its output's cell (`vm_obj_link_direct`); other blocks read it by name (`msg["key"]`) or position (`msg[i]`) through accessors the app keeps walked (a `dynamic` folder). Mapping decided: a number is `F`; an object is a `PTR` with named children (keys up to 15 characters); an array of numbers is one `F` array; a 2-D array is a `PTR` of 1-D arrays; a string is `STR`. Open: `null` and empty values (an object cannot be empty: probably an unwired cell).
- **Retained values as a record:** a snapshot as `DATA` frames in a record would make readback symmetric with the rest; for now a plain NVS blob.
- **Start with no program:** `EXEC` normal on an empty VM succeeds (`vm_loader_verify_complete` checks only an open program) and runs zero blocks. Undecided whether this should be an error.
- **Lost answers:** a request now and then gets no answer on the USB-Serial-JTAG link (about twice in 1500 requests, not repeatable); no cause found.

## Tests

`.claude/skills/runit-esp/scripts/vm_suite.py` (`--port COM9`, runs on the PCB; `--only`, `--list`, `--skip-hw`): the wire (every op truncated and malformed, the direction byte, fail closed, IDs), objects, accessors, supervisor modes (scan / ONCE, block / NEXT, pause, restart, speed) and what STATUS reads of them, runtime writes and the override ring, telemetry (limits, split objects, trees, a frame as a command), retained values, loads and limits, and every block (VM_BLOCKS.MD "Tests"). Not covered: a latched device fault (`vm_stop_on_fault`), the heap-object limit (`CONFIG_VM_DYN_MAX`), BLE as the telemetry transport.
