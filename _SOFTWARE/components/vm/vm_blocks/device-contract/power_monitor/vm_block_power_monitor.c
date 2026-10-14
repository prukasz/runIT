#include "vm_block_power_monitor.h"
#include "sys_power_monitor.h"
#include "vm_blocks.h"

_Static_assert(sizeof(pmon_args_GET_VOLTAGE_t) == 1 && sizeof(pmon_args_GET_CURRENT_t) == 1 &&
                   sizeof(pmon_args_GET_POWER_t) == 1 && sizeof(pmon_args_SET_ALERT_t) == 6,
               "a POWER_MONITOR op's argument struct changed: update the table of the POWER_MONITOR block");

static const vm_dev_op_t k_ops[] = {
    VM_DEV_OP(PMON_OP_GET_VOLTAGE, VM_DEV_ACT_READ, VM_DEV_R_I32, 1, VM_DEV_F_U8),
    VM_DEV_OP(PMON_OP_GET_CURRENT, VM_DEV_ACT_READ, VM_DEV_R_I32, 1, VM_DEV_F_U8),
    VM_DEV_OP(PMON_OP_GET_POWER, VM_DEV_ACT_READ, VM_DEV_R_I32, 1, VM_DEV_F_U8),
    VM_DEV_OP(PMON_OP_SET_ALERT, VM_DEV_ACT_CHANGE, VM_DEV_R_NONE, 3, VM_DEV_F_U8, VM_DEV_F_U8, VM_DEV_F_I32),
};
static const vm_dev_contract_t k_contract = {.ops = k_ops, .count = ARRAY_LEN(k_ops), .naddr = 1};

bool vm_verify_power_monitor(vm_block_h b) {
  return vm_device_check(b, &k_contract);
}

void vm_blk_power_monitor(vm_block_h b) {
  vm_device_run(b, &k_contract);
}

VM_BLOCK_TYPE_DEFINE(VM_BLK_POWER_MONITOR, .run = vm_blk_power_monitor, .check = vm_verify_power_monitor, .min_in = 0,
                     .max_in = VM_DEV_FIELDS_MAX, .min_q = 0, .max_q = 1, .state_len = sizeof(vm_block_device_data_t));
