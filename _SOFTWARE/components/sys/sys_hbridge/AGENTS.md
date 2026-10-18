# sys_hbridge

Spec: `SYS_HBRIDGE.MD` (this folder).

- Every change in this folder is followed by an edit of this file, if it is needed.
- The op numbers (0 SET_DRIVE, 1 BRAKE, 2 COAST, 3 GET_CURRENT), the event numbers and their flags (all NOTIFY), the fault reasons (`sys_hbridge_fault_reason_e`, stored by drivers and read by the VM block) and the argument layouts (`hbridge_drive_args_t` 3 B, `hbridge_channel_args_t` 1 B) are wire format: do not renumber or re-layout them. New ones go at the end.
- Changing the contract table, an argument struct or an event requires an update in `templates/`: run `sh templates/sync.sh` (regenerates `generated/`, validates). Commit with `--check` green. The op numbers, args structs and field offsets in `contract_hbridge.json` must match the table.
- `SYS_HBRIDGE_DRIVE_MAX` is `IO_PWM_DUTY_MAX` (4095). The VM block's scale assumes 12 bits (`_Static_assert` in `vm_block_hbridge.c`). Changing the PWM resolution changes the contract: update the block and the descriptor together.
- The contract ID (`SYS_DEVICE_CONTRACT_HBRIDGE`) is in `sys_device.h`, not here.
- Chip specifics are the driver's own ops (`0xFFxx`), never new ops in this table.
- The wrappers only build `sys_device_call`. No device logic goes here.
- The CMake `REQUIRES` lists `sys_event`, because the header includes `sys_event.h` for the event flags.
