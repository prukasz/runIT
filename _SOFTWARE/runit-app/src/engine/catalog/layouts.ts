/*
 * Facts about a block's state that generated/blocks does not carry: where the values sit in the 56-byte state every device block
 * shares (`vm_block_device_data_t`, runit-esp/components/vm/vm_blocks/device-contract/vm_block_device.h). The size of a state is not here:
 * the descriptors publish it as `state_len` (templates/crosscheck.py compares it with the C), and the catalog reads it.
 * `tests/engine/catalog/catalog.test.ts` reads the struct from the C and holds these offsets against it.
 */

/**
 * `vm_block_device_data_t`: where the device block's values sit in its state.
 * u16 device_id @0, u16 op @2, u16 idle_op @4, u8 flags @6, u8 dir @7 (cmd_dir_e of `op`), i32 k[4] @8, i32 idle_k[4] @24, i32 last[4] @40.
 * `k` holds the constant of each input of the operation while that input is unwired (the bits of a float for a scaled input);
 * `idle_k` the inputs of the idle operation after its address inputs; `last` and `flags` are runtime state, zero in a program.
 */
export const DEVICE_STATE = {
  deviceId: 0,
  op: 2,
  idleOp: 4,
  flags: 6,
  dir: 7,
  k: 8,
  idleK: 24,
  last: 40,
  /** `VM_DEV_FIELDS_MAX`: constants (and inputs) per operation. */
  fields: 4,
} as const
