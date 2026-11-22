#include "vm_block_dc_motor.h"
#include "device_dc_motor.h"
#include "vm_blocks.h"

_Static_assert(sizeof(dc_motor_speed_t) == 2, "a DC motor op's argument struct changed: update the table of the block");

static const vm_dev_op_t k_ops[] = {
    {.op = DC_MOTOR_OP_SPEED,
     .act = VM_DEV_ACT_CHANGE,
     .result = VM_DEV_R_NONE,
     .nfield = 1,
     .ftype = {VM_DEV_F_I16},
     .scale = {10.0f},                  // percent in the block, tenths on the wire
     .lim = {DC_MOTOR_SPEED_MAX}},      // past full is full: held, reported once
    {.op = DC_MOTOR_OP_STOP, .act = VM_DEV_ACT_EDGE, .result = VM_DEV_R_NONE, .nfield = 0},
    {.op = DC_MOTOR_OP_BRAKE, .act = VM_DEV_ACT_EDGE, .result = VM_DEV_R_NONE, .nfield = 0},
    {.op = DC_MOTOR_OP_COAST, .act = VM_DEV_ACT_EDGE, .result = VM_DEV_R_NONE, .nfield = 0},
    {.op = DC_MOTOR_OP_OUTPUT, .act = VM_DEV_ACT_READ, .result = VM_DEV_R_I32, .nfield = 0, .rscale = {10.0f}},
    VM_DEV_OP(DC_MOTOR_OP_CURRENT, VM_DEV_ACT_READ, VM_DEV_R_I32, 0),
    VM_DEV_OP(DC_MOTOR_OP_STALLED, VM_DEV_ACT_READ, VM_DEV_R_U8, 0),
};
static const vm_dev_contract_t k_contract = {.ops = k_ops, .count = ARRAY_LEN(k_ops), .naddr = 0};

bool vm_verify_dc_motor(vm_block_h b) {
  return vm_device_check(b, &k_contract);
}

void vm_blk_dc_motor(vm_block_h b) {
  vm_device_run(b, &k_contract);
}

VM_BLOCK_TYPE_DEFINE(VM_BLK_DC_MOTOR, .run = vm_blk_dc_motor, .check = vm_verify_dc_motor, .min_in = 0, .max_in = 1,
                     .min_q = 0, .max_q = 1, .state_len = sizeof(vm_block_device_data_t));
