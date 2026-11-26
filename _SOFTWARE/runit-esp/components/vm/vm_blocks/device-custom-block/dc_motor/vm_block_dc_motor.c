#include "vm_block_dc_motor.h"
#include "vm_blocks.h"

/* The DC motor's own ops: its driver is one source nothing includes, so the numbers are its table's
 * (OWN_DC_MOTOR_OPERATIONS, device_dc_motor.c), checked against it by the app's tests. SPEED's value is one i16 in
 * tenths of a percent, at most DC_MOTOR_SPEED_MAX either way (the driver's). */
#define OWN_DC_MOTOR_OP_SPEED   SYS_DEVICE_OWN_OP(0x00)
#define OWN_DC_MOTOR_OP_OUTPUT  SYS_DEVICE_OWN_OP(0x01)
#define OWN_DC_MOTOR_OP_STOP    SYS_DEVICE_OWN_OP(0x02)
#define OWN_DC_MOTOR_OP_BRAKE   SYS_DEVICE_OWN_OP(0x03)
#define OWN_DC_MOTOR_OP_COAST   SYS_DEVICE_OWN_OP(0x04)
#define OWN_DC_MOTOR_OP_CURRENT SYS_DEVICE_OWN_OP(0x06)
#define OWN_DC_MOTOR_OP_STALLED SYS_DEVICE_OWN_OP(0x07)
#define DC_MOTOR_SPEED_MAX      1000  // 100 % in tenths

static const vm_dev_op_t k_ops[] = {
    {.op = OWN_DC_MOTOR_OP_SPEED,
     .act = VM_DEV_ACT_CHANGE,
     .result = VM_DEV_R_NONE,
     .nfield = 1,
     .ftype = {VM_DEV_F_I16},
     .scale = {10.0f},                  // percent in the block, tenths on the wire
     .lim = {DC_MOTOR_SPEED_MAX}},      // past full is full: held, reported once
    {.op = OWN_DC_MOTOR_OP_STOP, .act = VM_DEV_ACT_EDGE, .result = VM_DEV_R_NONE, .nfield = 0},
    {.op = OWN_DC_MOTOR_OP_BRAKE, .act = VM_DEV_ACT_EDGE, .result = VM_DEV_R_NONE, .nfield = 0},
    {.op = OWN_DC_MOTOR_OP_COAST, .act = VM_DEV_ACT_EDGE, .result = VM_DEV_R_NONE, .nfield = 0},
    {.op = OWN_DC_MOTOR_OP_OUTPUT, .act = VM_DEV_ACT_READ, .result = VM_DEV_R_I32, .nfield = 0, .rscale = {10.0f}},
    VM_DEV_OP(OWN_DC_MOTOR_OP_CURRENT, VM_DEV_ACT_READ, VM_DEV_R_I32, 0),
    VM_DEV_OP(OWN_DC_MOTOR_OP_STALLED, VM_DEV_ACT_READ, VM_DEV_R_U8, 0),
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
