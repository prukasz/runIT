#include "vm_block_servo.h"
#include "vm_blocks.h"

/* The servo's own ops: its driver is one source nothing includes, so the numbers are its table's (OWN_SERVO_OPERATIONS,
 * device_servo.c), checked against it by the app's tests. TARGET's and TRIM's value is one i16. */
#define OWN_SERVO_OP_HOME    SYS_DEVICE_OWN_OP(0x00)
#define OWN_SERVO_OP_TRIM    SYS_DEVICE_OWN_OP(0x01)
#define OWN_SERVO_OP_TARGET  SYS_DEVICE_OWN_OP(0x02)
#define OWN_SERVO_OP_RELEASE SYS_DEVICE_OWN_OP(0x04)

static const vm_dev_op_t k_ops[] = {
    {.op = OWN_SERVO_OP_TARGET,
     .act = VM_DEV_ACT_CHANGE,
     .result = VM_DEV_R_NONE,
     .nfield = 1,
     .ftype = {VM_DEV_F_I16},
     .scale = {10.0f}},  // position in the servo's unit, tenths on the wire
    {.op = OWN_SERVO_OP_HOME, .act = VM_DEV_ACT_EDGE, .result = VM_DEV_R_NONE, .nfield = 0},
    {.op = OWN_SERVO_OP_RELEASE, .act = VM_DEV_ACT_EDGE, .result = VM_DEV_R_NONE, .nfield = 0},
    VM_DEV_OP(OWN_SERVO_OP_TRIM, VM_DEV_ACT_CHANGE, VM_DEV_R_NONE, 1, VM_DEV_F_I16),
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
