#include "vm_block_power_usb_pd.h"
#include "sys_power_usb_pd.h"
#include "vm_blocks.h"

_Static_assert(sizeof(PD_value_SETTINGS_t) == 8 && sizeof(sys_power_usb_pd_limits_t) == 8,
               "a POWER_USB_PD struct changed: update the table of the POWER_USB_PD block");

static const vm_dev_op_t k_ops[] = {
    {.op = PD_OP_SETTINGS,
     .act = VM_DEV_ACT_CHANGE,
     .result = VM_DEV_R_NONE,
     .nfield = 2,
     .ftype = {VM_DEV_F_U32, VM_DEV_F_U32},
     .scale = {1000.0f, 0}},  // voltage in volts (mV on the wire), current in mA
    {.op = PD_OP_LIMITS, .act = VM_DEV_ACT_READ, .result = VM_DEV_R_I32X2, .nfield = 0, .rscale = {1000.0f, 0}},  // V, mA
};
static const vm_dev_contract_t k_contract = {.ops = k_ops, .count = ARRAY_LEN(k_ops), .naddr = 0};

bool vm_verify_power_usb_pd(vm_block_h b) {
  return vm_device_check(b, &k_contract);
}

void vm_blk_power_usb_pd(vm_block_h b) {
  vm_device_run(b, &k_contract);
}

VM_BLOCK_TYPE_DEFINE(VM_BLK_POWER_USB_PD, .run = vm_blk_power_usb_pd, .check = vm_verify_power_usb_pd, .min_in = 0,
                     .max_in = VM_DEV_FIELDS_MAX, .min_q = 0, .max_q = 2, .state_len = sizeof(vm_block_device_data_t));
