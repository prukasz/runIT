# vm_blocks

Spec: `VM_BLOCKS.MD` (this folder). The runtime: `../vm_core/VM_CORE.MD`. One user guide per block: `<name>.md` next to its code.

- Every change in this folder is followed by an edit of this file, if it is needed.
- Do not add references to `legacy docs` unless the user asks for them.
- The block type numbers (`vm_core/block/vm_block_ids.h`), the state structs (sizes pinned by `_Static_assert`, `vm_block_device_data_t` 56 B), the pin order and the option bits are program format: an uploaded program holds them. Do not renumber or re-layout them; new fields go at the end and change `state_len`.
- Every block has a `block_<name>.json` (schema `templates/vm-block.schema.json`) and a `<name>.md`. Changing a block's pins, state, options or behaviour changes both: update the descriptor and the guide with the code, then run `sh templates/sync.sh` (validates, checks `wire_id` and `state_len` against the code, refreshes `generated/blocks/`). Commit with `--check` green.
- The `<name>.md` guides are for the user of the block: what it does, its pins and settings, its behaviour. No C names, no wire details.
- A block registers itself with `VM_BLOCK_TYPE_DEFINE` at the end of its `.c`. The shape (`min_in` .. `required_in`, `state_len`) is checked at load by `vm_block_verify`; the body never re-checks it. Private state is checked once in `.check`.
- A body runs every pass: it decides itself whether it acts (`vm_block_is_enabled`, `vm_block_triggered`). Not acting keeps the outputs and drops ENO quietly; only IF / SWITCH clear their outputs (flow).
- A body does not allocate, block or touch flash. Device calls go through `BLOCK_CALL`, so a failure is reported with the block.
- Device blocks: one table per contract over the shared engine (`device-contract/vm_block_device.c`). A `_Static_assert` per row ties the fields to the contract's argument struct. Contract blocks depend only on the `sys_*` contract headers; a device's own ops go to `device-custom-block/<chip>/` and add `device_<chip>` to `REQUIRES`.
- A new block goes into `CMakeLists.txt` (`SRCS`, `INCLUDE_DIRS`); the component is `WHOLE_ARCHIVE` and builds at `-O2`.
- New behaviour gets a case in `.claude/skills/runit-esp/scripts/vm_suite.py`.
