#include "vm_block_servo.h"
#include "device_servo.h"
#include "vm_blocks.h"

_Static_assert(sizeof(servo_set_t) == 2 && sizeof(servo_trim_t) == 2,
               "a servo op's argument struct changed: update the table of the SERVO block");

static const vm_dev_op_t k_ops[] = {
    //@op SERVO_OP_SET @alias Position @description Moves the servo to a position. @face Move to {position}
    //@op-in position @alias Position @unit deg or %
    {.op = SERVO_OP_SET,
     .act = VM_DEV_ACT_CHANGE,
     .result = VM_DEV_R_NONE,
     .nfield = 1,
     .ftype = {VM_DEV_F_I16},
     .scale = {10.0f}},  // position in the servo's unit, tenths on the wire
    //@op SERVO_OP_HOME @alias Home @description Moves the servo to its default position (plus the trim) once on each rising enable. @face Home
    {.op = SERVO_OP_HOME, .act = VM_DEV_ACT_EDGE, .result = VM_DEV_R_NONE, .nfield = 0},
    //@op SERVO_OP_RELEASE @alias Release @description Stops the pulse so the servo goes limp, once on each rising enable. @face Release
    {.op = SERVO_OP_RELEASE, .act = VM_DEV_ACT_EDGE, .result = VM_DEV_R_NONE, .nfield = 0},
    //@op SERVO_OP_SET_TRIM @alias Trim @description Sets a correction added to every move. @face Trim {trim}
    //@op-in trim @alias Trim @unit deg or us
    VM_DEV_OP(SERVO_OP_SET_TRIM, VM_DEV_ACT_CHANGE, VM_DEV_R_NONE, 1, VM_DEV_F_I16),
};
static const vm_dev_contract_t k_contract = {.ops = k_ops, .count = ARRAY_LEN(k_ops), .naddr = 0};

bool vm_verify_servo(vm_block_h b) {
  return vm_device_check(b, &k_contract);
}

void vm_blk_servo(vm_block_h b) {
  vm_device_run(b, &k_contract);
}

VM_BLOCK_TYPE_DEFINE(VM_BLK_SERVO, .run = vm_blk_servo, .check = vm_verify_servo, .min_in = 0, .max_in = 1, .min_q = 0,
                     .max_q = 0, .state_len = sizeof(vm_block_device_data_t));
