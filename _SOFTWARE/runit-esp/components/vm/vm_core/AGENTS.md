# vm_core

Spec: `VM_CORE.MD` (this folder). The palette: `../vm_blocks/VM_BLOCKS.MD`.

- Every change in this folder is followed by an edit of this file, if it is needed.
- `ROUTER_KEY_VM` (0x0B), the op numbers (`VM_OP_*`, 0x01 RESET to 0x0A STATUS), the records in `vm_wire.h` (layouts pinned by `_Static_assert`), the wire object head (`vm_wire_head_t`, flag bits), the side of each op (`ROUTER_R` / `ROUTER_W` in `vm_router.c`), `vm_exec_command_e`, the block type numbers in `block/vm_block_ids.h` and the error tags in `ERR_VM_MAP` (`ERR_MOD_VM` = 15) are wire format: do not renumber or re-layout them. New ones go at the end; a removed block number stays unused.
- Changing an error tag, a `#ref-*` enum (`//@alias` comments) or a `#@limit` Kconfig option requires an update in `templates/`: run `sh templates/sync.sh` (regenerates `enums.json` / `errors.json` / limits, refreshes `runit-app/generated/`, validates). Commit with `--check` green.
- The core knows no block: never include a `vm_blocks` header here. A block type is only what `g_vm_block_types[]` holds.
- A telemetry frame is a command of this router without the direction byte (`[0x0B][DATA | ADD_OBJECTS][count]`). A change to a record changes both directions: keep `vm_router.c`, the loader and `vm_sub.c` on the same `vm_wire.h` structs.
- Everything from the wire is untrusted: check IDs, counts and lengths before storing. A failing load op goes through `vm_loader_abort(err, VM_OP_*)` (fail closed); a running program is never changed by ADD_*, and DATA while running goes through `vm_override`.
- The hot path (pass, accessors, block helpers) does not allocate and does not block. Only CLONE allocates (heap objects, `vm_obj_dyn`). Flash writes (retained values) run on the retain task, never on the VM task.
- Freshness (`upd`) is cleared only by the supervisor at the end of a pass. ENO false is set quietly.
- Actions are requested (`vm_exec_request_action`), never run inside a pass.
- Errors that end where they are raised use `VM_RAISE`; block errors go through `BLOCK_CALL` / `vm_block_report_error`, so they carry the block.
- The component builds at `-O2` and links `WHOLE_ARCHIVE` (the error tags register from a constructor): keep both in `CMakeLists.txt`.
- New behaviour gets a case in `.claude/skills/runit-esp/scripts/vm_suite.py`.
