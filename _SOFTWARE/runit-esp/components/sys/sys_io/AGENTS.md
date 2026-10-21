# sys_io

Spec: `SYS_IO.MD` (this folder).

- Every change in this folder is followed by an edit of this file, if it is needed.
- Op numbers (`IO_OPERATIONS`, `PINS_OPERATIONS`), event numbers (`SYS_IO_EVENTS`), the enum values of `sys_io_mode_e` and `sys_io_intr_mode_e`, the error tags and the `SYS_IO_PIN_NONE` encoding (device `0xFFFF`) are wire format or stored by devices: do not renumber them. New ones go at the end.
- Changing a contract table, an enum, the error map or `contract_io.json` / `contract_pins.json` requires an update in `templates/`: run `sh templates/sync.sh` (regenerates `enums.json` / `errors.json`, refreshes `runit-app/generated/`, validates). Commit with `--check` green. A descriptor row must name the same op number and args struct as its table row. A new op that takes a pin gets its `pin_state` in `contract_io.json` (templates/AGENTS.md "Command pins"), matching the mode the handler checks (`pin_in_mode`): the app lists only such pins.
- The wrappers in `sys_io.c` only build `sys_device_call`; no device-specific logic goes there. The device's own ops do the work.
- `sys_io_prepare` and `sys_io_release` tolerate `ERR_DEV_UNKNOWN_OP` only. `sys_io_prepare_input` also falls back to a plain input on `ERR_IO_MODE_UNSUPPORTED`. Every other failure is returned.
- `sys_pins_take_*` and `sys_pins_release` undo exactly what a take did: a new step in a take needs its undo in the release, and a new flag in `sys_pins_owned_t` needs its reset there.
- The PINS lock is bookkeeping: LOCK / UNLOCK are trusted, and the contract has no owner ID.
