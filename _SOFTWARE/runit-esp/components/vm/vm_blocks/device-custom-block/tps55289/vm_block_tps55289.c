#include "vm_block_tps55289.h"
#include "device_tps55289.h"
#include "vm_blocks.h"

_Static_assert(sizeof(tps55289_protection_t) == 4, "tps55289_protection_t changed: update the table of the TPS55289 block");

static const vm_dev_op_t k_ops[] = {
    VM_DEV_OP(TPS55289_OP_PROTECTION, VM_DEV_ACT_CHANGE, VM_DEV_R_NONE, 4, VM_DEV_F_U8, VM_DEV_F_U8, VM_DEV_F_U8,
              VM_DEV_F_U8),
    {.op = TPS55289_OP_PROTECTION, .act = VM_DEV_ACT_READ, .result = VM_DEV_R_U8X4, .nfield = 0},
};
static const vm_dev_contract_t k_contract = {.ops = k_ops, .count = ARRAY_LEN(k_ops), .naddr = 0};

bool vm_verify_tps55289(vm_block_h b) {
  return vm_device_check(b, &k_contract);
}

void vm_blk_tps55289(vm_block_h b) {
  vm_device_run(b, &k_contract);
}

VM_BLOCK_TYPE_DEFINE(VM_BLK_TPS55289, .run = vm_blk_tps55289, .check = vm_verify_tps55289, .min_in = 0,
                     .max_in = VM_DEV_FIELDS_MAX, .min_q = 0, .max_q = 4, .state_len = sizeof(vm_block_device_data_t));
