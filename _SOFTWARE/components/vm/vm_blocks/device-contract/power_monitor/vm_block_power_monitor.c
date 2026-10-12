#include "vm_block_power_monitor.h"
#include "sys_power_monitor.h"
#include "vm_blocks.h"

_Static_assert(sizeof(pmon_args_GET_VOLTAGE_t) == 1 && sizeof(pmon_args_GET_CURRENT_t) == 1 &&
                   sizeof(pmon_args_GET_POWER_t) == 1 && sizeof(pmon_args_SET_ALERT_t) == 6,
               "a POWER_MONITOR op's argument struct changed: update the table of the POWER_MONITOR block");

static const vm_dev_op_t k_ops[] = {
    //@op PMON_OP_GET_VOLTAGE @alias Read voltage @description Reads the voltage of a channel. @face Voltage of {channel}
    //@op-in channel @alias Channel @id pin
    //@op-out voltage @alias Voltage @unit mV
    VM_DEV_OP(PMON_OP_GET_VOLTAGE, VM_DEV_ACT_READ, VM_DEV_R_I32, 1, VM_DEV_F_U8),
    //@op PMON_OP_GET_CURRENT @alias Read current @description Reads the current of a channel. @face Current of {channel}
    //@op-in channel @alias Channel @id pin
    //@op-out current @alias Current @unit mA
    VM_DEV_OP(PMON_OP_GET_CURRENT, VM_DEV_ACT_READ, VM_DEV_R_I32, 1, VM_DEV_F_U8),
    //@op PMON_OP_GET_POWER @alias Read power @description Reads the power of a channel. @face Power of {channel}
    //@op-in channel @alias Channel @id pin
    //@op-out power @alias Power @unit mW
    VM_DEV_OP(PMON_OP_GET_POWER, VM_DEV_ACT_READ, VM_DEV_R_I32, 1, VM_DEV_F_U8),
    //@op PMON_OP_SET_ALERT @alias Set current alert @description Arms a hardware alert that publishes an event when the current crosses a threshold. @face {channel}: {alert} at {threshold}
    //@op-in channel @alias Channel @id pin
    //@op-in alert @alias Alert @choices 0=Warning,1=Critical
    //@op-in threshold @alias Threshold @unit mA
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
