#include "vm_block_lifecycle.h"
#include "sys_device.h"
#include "vm_blocks.h"

_Static_assert(sizeof(sys_device_args_RESET_t) == 0 && sizeof(sys_device_args_SUSPEND_t) == 0 &&
                   sizeof(sys_device_args_RESUME_t) == 0,
               "a lifecycle op takes arguments now: update the table of the LIFECYCLE block");

static const vm_dev_op_t k_ops[] = {
    //@op SYS_DEVICE_OP_RESET @alias Reset @description Puts the device's outputs off and its settings back to defaults. @face Reset
    {.op = SYS_DEVICE_OP_RESET, .act = VM_DEV_ACT_EDGE, .result = VM_DEV_R_NONE, .nfield = 0},
    //@op SYS_DEVICE_OP_SUSPEND @alias Suspend @description Makes the device's outputs safe; it refuses writes until it is resumed. @face Suspend
    {.op = SYS_DEVICE_OP_SUSPEND, .act = VM_DEV_ACT_EDGE, .result = VM_DEV_R_NONE, .nfield = 0},
    //@op SYS_DEVICE_OP_RESUME @alias Resume @description Lets a suspended device work again. @face Resume
    {.op = SYS_DEVICE_OP_RESUME, .act = VM_DEV_ACT_EDGE, .result = VM_DEV_R_NONE, .nfield = 0},
};
static const vm_dev_contract_t k_contract = {.ops = k_ops, .count = ARRAY_LEN(k_ops), .sweep = true, .naddr = 0};

bool vm_verify_lifecycle(vm_block_h b) {
  return vm_device_check(b, &k_contract);
}

void vm_blk_lifecycle(vm_block_h b) {
  vm_device_run(b, &k_contract);
}

VM_BLOCK_TYPE_DEFINE(VM_BLK_LIFECYCLE, .run = vm_blk_lifecycle, .check = vm_verify_lifecycle, .min_in = 0, .max_in = 0,
                     .min_q = 0, .max_q = 0, .state_len = sizeof(vm_block_device_data_t));
