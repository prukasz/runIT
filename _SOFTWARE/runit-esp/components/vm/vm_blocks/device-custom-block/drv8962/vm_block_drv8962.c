#include "vm_block_drv8962.h"
#include "sys_hbridge_contract.h"
#include "vm_blocks.h"

/* The DRV8962's own ops: its driver is one source nothing includes, so the numbers are its table's
 * (OWN_DRV8962_OPERATIONS, device_drv8962.c), checked against it by the app's tests. CURRENT_LIMIT's value is one u32
 * (mA), CLEAR_FAULT's and FAULT's key a channel (hbridge_channel_args_t), FAULT's answer two bytes (fault, reason). */
#define OWN_DRV8962_OP_CURRENT_LIMIT SYS_DEVICE_OWN_OP(0x00)
#define OWN_DRV8962_OP_CLEAR_FAULT   SYS_DEVICE_OWN_OP(0x01)
#define OWN_DRV8962_OP_FAULT         SYS_DEVICE_OWN_OP(0x02)

_Static_assert(sizeof(hbridge_channel_args_t) == 1, "the DRV8962 block's channel field is a u8");

/* The channel is the address field (naddr 1). The current limit is the chip's and takes no channel: its one field is a
   u32, so as an idle operation it pairs only with itself (the field types must match). */
static const vm_dev_op_t k_ops[] = {
    VM_DEV_OP(OWN_DRV8962_OP_CURRENT_LIMIT, VM_DEV_ACT_CHANGE, VM_DEV_R_NONE, 1, VM_DEV_F_U32),
    VM_DEV_OP(OWN_DRV8962_OP_CLEAR_FAULT, VM_DEV_ACT_EDGE, VM_DEV_R_NONE, 1, VM_DEV_F_U8),
    VM_DEV_OP(OWN_DRV8962_OP_FAULT, VM_DEV_ACT_READ, VM_DEV_R_U8X2, 1, VM_DEV_F_U8),
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
