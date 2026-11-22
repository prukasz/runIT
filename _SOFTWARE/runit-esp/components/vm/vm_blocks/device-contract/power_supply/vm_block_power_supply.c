#include "vm_block_power_supply.h"
#include "sys_power_supply.h"
#include "vm_blocks.h"

_Static_assert(sizeof(SUPPLY_value_SOURCE_t) == 1, "the POWER_SUPPLY SELECT argument struct changed: update the table");

static const vm_dev_op_t k_ops[] = {
    VM_DEV_OP(SUPPLY_OP_SOURCE, VM_DEV_ACT_CHANGE, VM_DEV_R_NONE, 1, VM_DEV_F_U8),
    {.op = SUPPLY_OP_SOURCE, .act = VM_DEV_ACT_READ, .result = VM_DEV_R_U8, .nfield = 0},
};
static const vm_dev_contract_t k_contract = {.ops = k_ops, .count = ARRAY_LEN(k_ops), .naddr = 0};

bool vm_verify_power_supply(vm_block_h b) {
  return vm_device_check(b, &k_contract);
}

void vm_blk_power_supply(vm_block_h b) {
  vm_device_run(b, &k_contract);
}

VM_BLOCK_TYPE_DEFINE(VM_BLK_POWER_SUPPLY, .run = vm_blk_power_supply, .check = vm_verify_power_supply, .min_in = 0,
                     .max_in = VM_DEV_FIELDS_MAX, .min_q = 0, .max_q = 1, .state_len = sizeof(vm_block_device_data_t));
