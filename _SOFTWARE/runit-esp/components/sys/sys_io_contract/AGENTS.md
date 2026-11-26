# sys_io_contract

Spec: `SYS_IO_CONTRACT.MD` (this folder).

- Every change in this folder is followed by an edit of `SYS_IO_CONTRACT.MD` and of this file, if it is needed.
- Op numbers (`CONTRACT_IO_OPERATIONS`), event codes (`IO_EVENTS`) and their payloads, the enum values of `sys_io_mode_e` and `sys_io_intr_mode_e`, the error tags and the `SYS_IO_PIN_NONE` encoding are wire format or stored by devices: do not renumber them. New ones go at the end.
- Changing the contract table, an enum, the events, the error map or `contract_io.json` requires an update in `templates/`: run `sh templates/sync.sh`. A descriptor row names the same op number and args struct as its table row. A new op that takes a pin gets its `pin_state` in `contract_io.json` (templates/AGENTS.md "Command pins"), matching the mode its handlers check: the app lists only such pins.
- The wrappers in `sys_io_contract.c` are only the ones with logic of their own; a plain call is `SYS_IO_WRITE` / `SYS_IO_READ`. No device-specific logic goes there.
- `sys_io_prepare` and `sys_io_release` tolerate `ERR_DEV_UNKNOWN_OP` only. `sys_io_prepare_input` also falls back to a plain input on `ERR_IO_MODE_UNSUPPORTED`. Every other failure is returned.
- `sys_io_claim_*` and their release undo exactly what a take did: a new step in a take needs its undo in `release()`, and a new flag in `sys_io_claim_t` needs its reset there. A take adds the pin as a resource of its owner before it touches the pin; a driver never releases a pin itself.
- Every `sys_io_claim_*` locks its pin: an unlocked claim is released by a RESET of the pin's device (the GPIO's releases every unlocked pin) and writable by anyone. Never add a take that leaves it unlocked.
- LOCK and UNLOCK stay `SYS_DEVICE_OP_LOCAL` and `SYS_DEVICE_OP_ALWAYS`: drivers only (`sys_io_claim`), never the app or the VM. A lock keeps its owner (`sys_device_caller()` of the LOCK). The owner writes its pin with the contract's ordinary ops: every write handler calls `sys_io_pins_check_free`, which passes a free pin and the owner's own. Never add an op that exists only for the owner of a locked pin, and never unlock a pin to change it.
- A pin with the owner `SYS_DEVICE_ID_NONE` is nobody's to write. Only the ESP32-S3 GPIO driver locks pins itself (the ones a native peripheral reserved in ESP-IDF); every other lock comes from the device that uses the pin.
