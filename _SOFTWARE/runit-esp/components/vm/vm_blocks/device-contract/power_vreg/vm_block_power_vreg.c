#include "vm_block_power_vreg.h"
#include "sys_power_vreg.h"
#include "vm_blocks.h"

_Static_assert(sizeof(vreg_value_ENABLE_t) == 1 && sizeof(vreg_value_VOLTAGE_t) == 4 &&
                   sizeof(vreg_value_CURRENT_t) == 4,
               "a POWER_VREG op's argument struct changed: update the table of the POWER_VREG block");

static const vm_dev_op_t k_ops[] = {
    VM_DEV_OP(VREG_OP_ENABLE, VM_DEV_ACT_CHANGE, VM_DEV_R_NONE, 1, VM_DEV_F_U8),
    VM_DEV_OP(VREG_OP_VOLTAGE, VM_DEV_ACT_CHANGE, VM_DEV_R_NONE, 1, VM_DEV_F_U32),
    VM_DEV_OP(VREG_OP_CURRENT, VM_DEV_ACT_CHANGE, VM_DEV_R_NONE, 1, VM_DEV_F_U32),
};
static const vm_dev_contract_t k_contract = {.ops = k_ops, .count = ARRAY_LEN(k_ops), .naddr = 0};

bool vm_verify_power_vreg(vm_block_h b) {
  return vm_device_check(b, &k_contract);
}

void vm_blk_power_vreg(vm_block_h b) {
  vm_device_run(b, &k_contract);
}

VM_BLOCK_TYPE_DEFINE(VM_BLK_POWER_VREG, .run = vm_blk_power_vreg, .check = vm_verify_power_vreg, .min_in = 0,
                     .max_in = VM_DEV_FIELDS_MAX, .min_q = 0, .max_q = 0, .state_len = sizeof(vm_block_device_data_t));
