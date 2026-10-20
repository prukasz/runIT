#include "vm_block_drv8962.h"
#include "device_drv8962.h"
#include "sys_hbridge.h"
#include "vm_blocks.h"

_Static_assert(sizeof(drv8962_limit_t) == 4 && sizeof(hbridge_channel_args_t) == 1 && sizeof(drv8962_fault_t) == 2,
               "a DRV8962 op's struct changed: update the table of the DRV8962 block");

/* The channel is the address field (naddr 1). The current limit is the chip's and takes no channel: its one field is a
   u32, so as an idle operation it pairs only with itself (the field types must match). */
static const vm_dev_op_t k_ops[] = {
    VM_DEV_OP(DRV8962_OP_CURRENT_LIMIT, VM_DEV_ACT_CHANGE, VM_DEV_R_NONE, 1, VM_DEV_F_U32),
    VM_DEV_OP(DRV8962_OP_CLEAR_FAULT, VM_DEV_ACT_EDGE, VM_DEV_R_NONE, 1, VM_DEV_F_U8),
    VM_DEV_OP(DRV8962_OP_FAULT, VM_DEV_ACT_READ, VM_DEV_R_U8X2, 1, VM_DEV_F_U8),
};
static const vm_dev_contract_t k_contract = {.ops = k_ops, .count = ARRAY_LEN(k_ops), .naddr = 1};

bool vm_verify_drv8962(vm_block_h b) {
  return vm_device_check(b, &k_contract);
}

void vm_blk_drv8962(vm_block_h b) {
  vm_device_run(b, &k_contract);
}

VM_BLOCK_TYPE_DEFINE(VM_BLK_DRV8962, .run = vm_blk_drv8962, .check = vm_verify_drv8962, .min_in = 0, .max_in = 2,
                     .min_q = 0, .max_q = 2, .state_len = sizeof(vm_block_device_data_t));
