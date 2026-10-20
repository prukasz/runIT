#pragma once
#include "sys_device.h"
#include "vm_block_helpers.h"

/*
 *            ---------------
 *  ->EN      |             | ->ENO
 *  (->ARG0)  |  <contract> | ->RESULT
 *  (->ARG1)  |             |
 *  (->ARG2)  ---------------
 *
 *  One block type per device contract (IO, HBRIDGE, POWER_MONITOR, POWER_SUPPLY, POWER_USB_PD, POWER_VREG), one
 *  engine for all of them. The block's state names the device and the operation of the contract; the operation's
 *  argument fields are the inputs, in the order of the argument struct (without the device and the op), and the
 *  result of a read operation is output 0.
 *
 *  - An input that is not wired uses its constant from the state (`k`). A wired input is converted to the field's type
 *    (rounded, saturated); the device range-checks. A field with a scale (HBRIDGE drive: percent, wire steps = x40.95)
 *    takes a float, in the block's unit, from the input and from the constant alike (the constant is stored as a float).
 *  - Operations come in three kinds (vm_dev_act_e):
 *      CHANGE  a setting: sent on the first enabled pass and whenever an argument changed (write-on-change);
 *      EDGE    an action (toggle, brake, coast, reset): sent once per rising edge of the enable;
 *      READ    a reading: asked on every enabled pass, written to output 0 (gate it with PERIODIC to ask less often).
 *  - While the block is not enabled it can send one idle operation of the same contract (`idle_op`, 0 = hold what
 *    the device has): the address fields (pin / channel; none for a contract of a single thing) are the ones last sent, the other fields are `idle_k`.
 *  - A read result is output 0; a result of several values (USB_PD limits, a fault, the TPS55289 protection) fills
 *    outputs 0, 1, ... in the order the device answers.
 *  - ENO: a CHANGE / READ block is true while its device accepts the calls this pass; an EDGE block pulses on the pass
 *    it fired. A failed call is reported and clears ENO.
 *
 *  What an operation takes is in the contract's table (vm_block_device.c); the shape is checked once at load.
 */

#define VM_DEV_FIELDS_MAX 4u
#define VM_DEV_ARGS_BYTES 12u  // the packed argument fields of any op in the tables

typedef enum vm_dev_ftype_e {
  VM_DEV_F_NONE = 0,
  VM_DEV_F_U8 = 1,
  VM_DEV_F_I16 = 2,
  VM_DEV_F_U32 = 3,
  VM_DEV_F_I32 = 4,
} vm_dev_ftype_e;

typedef enum vm_dev_result_e {
  VM_DEV_R_NONE = 0,
  VM_DEV_R_U8 = 1,
  VM_DEV_R_I32 = 2,
  VM_DEV_R_I32X2 = 3,  // two i32: outputs 0 and 1
  VM_DEV_R_U8X2 = 4,   // two bytes: outputs 0 and 1
  VM_DEV_R_U8X4 = 5,   // four bytes: outputs 0..3
} vm_dev_result_e;

typedef enum vm_dev_act_e {
  VM_DEV_ACT_CHANGE = 0,
  VM_DEV_ACT_EDGE = 1,
  VM_DEV_ACT_READ = 2,
} vm_dev_act_e;

/* One operation of a contract: its op ID, how it is driven and its packed argument fields. */
typedef struct vm_dev_op_t {
  uint16_t op;
  uint8_t act;                     // vm_dev_act_e
  uint8_t result;                  // vm_dev_result_e (READ ops)
  uint8_t nfield;                  // argument fields (the first is the address: pin / channel)
  uint8_t ftype[VM_DEV_FIELDS_MAX];  // vm_dev_ftype_e
  float scale[VM_DEV_FIELDS_MAX];    // 0: the field is in wire units; else the block's value is a float that is multiplied by it
} vm_dev_op_t;

/* A table row: op ID, kind, result, field count and the field types (scale 0: wire units). */
#define VM_DEV_OP(op_, act_, res_, n_, ...) \
  {.op = (op_), .act = (act_), .result = (res_), .nfield = (n_), .ftype = {__VA_ARGS__}}

typedef struct vm_dev_contract_t {
  const vm_dev_op_t* ops;
  uint8_t count;
  uint8_t naddr;  // leading argument fields that address a thing (a pin, a channel): 1 for IO / HBRIDGE / POWER_MONITOR, 0 for a device with one thing
} vm_dev_contract_t;

#define VM_DEV_F_PREV_EN (1u << 0)  // the enable was true last pass
#define VM_DEV_F_SENT    (1u << 1)     // `last` holds what the device was told
#define VM_DEV_F_IDLE    (1u << 2)     // the idle operation was sent since the enable fell

typedef struct __attribute__((aligned(4))) {
  uint16_t device_id;  // The device the block works on.
  uint16_t op;  // What the block does with the device; it decides the block's inputs and outputs.
  uint16_t idle_op;  // What the block does once when its enable turns off (none: the device is left as it is).
  uint8_t flags;
  uint8_t dir;  // cmd_dir_e of `op`: a read fills the outputs, a write drives the device (idle_op is a write)
  int32_t k[VM_DEV_FIELDS_MAX];  // The value of each input of the operation while that input is not wired (the bits of a float for a scaled input).
  int32_t idle_k[VM_DEV_FIELDS_MAX];  // The inputs of the idle operation after its address inputs (same encoding).
  int32_t last[VM_DEV_FIELDS_MAX];
} vm_block_device_data_t;

_Static_assert(sizeof(vm_block_device_data_t) == 56, "vm_block_device_data_t must be 56 bytes");
#define VM_DEVICE_CUSTOM_LEN sizeof(vm_block_device_data_t)

/* The engine behind every contract block: the body and the load-time check, given the contract's table. */
void vm_device_run(vm_block_h b, const vm_dev_contract_t* contract);
bool vm_device_check(vm_block_h b, const vm_dev_contract_t* contract);
