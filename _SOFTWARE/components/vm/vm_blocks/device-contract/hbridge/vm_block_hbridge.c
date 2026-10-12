#include "vm_block_hbridge.h"
#include "sys_hbridge.h"
#include "vm_blocks.h"

_Static_assert(SYS_HBRIDGE_DRIVE_MAX == 4095, "the HBRIDGE block's scale (percent to steps) assumes 12 bits");
_Static_assert(sizeof(hbridge_args_SET_DRIVE_t) == 3 && sizeof(hbridge_args_BRAKE_t) == 1 &&
                   sizeof(hbridge_args_COAST_t) == 1 && sizeof(hbridge_args_GET_CURRENT_t) == 1,
               "an HBRIDGE op's argument struct changed: update the table of the HBRIDGE block");

static const vm_dev_op_t k_ops[] = {
    //@op HBRIDGE_OP_SET_DRIVE @alias Drive @description Drives a channel: positive forward, negative reverse (a half bridge only forward). @face {channel} at {drive}
    //@op-in channel @alias Channel @id pin
    //@op-in drive @alias Drive @unit % @min -100 @max 100
    {.op = HBRIDGE_OP_SET_DRIVE,
     .act = VM_DEV_ACT_CHANGE,
     .result = VM_DEV_R_NONE,
     .nfield = 2,
     .ftype = {VM_DEV_F_U8, VM_DEV_F_I16},
     .scale = {0, 40.95f}},  // channel, drive: percent in the block, 12-bit steps on the wire
    //@op HBRIDGE_OP_BRAKE @alias Brake @description Shorts the motor terminals once on each rising enable. @face Brake {channel}
    //@op-in channel @alias Channel @id pin
    VM_DEV_OP(HBRIDGE_OP_BRAKE, VM_DEV_ACT_EDGE, VM_DEV_R_NONE, 1, VM_DEV_F_U8),
    //@op HBRIDGE_OP_COAST @alias Coast @description Releases the motor (outputs off) once on each rising enable. @face Coast {channel}
    //@op-in channel @alias Channel @id pin
    VM_DEV_OP(HBRIDGE_OP_COAST, VM_DEV_ACT_EDGE, VM_DEV_R_NONE, 1, VM_DEV_F_U8),
    //@op HBRIDGE_OP_GET_CURRENT @alias Read current @description Reads the current of a channel. @face Read {channel}
    //@op-in channel @alias Channel @id pin
    //@op-out current @alias Current @unit mA
    VM_DEV_OP(HBRIDGE_OP_GET_CURRENT, VM_DEV_ACT_READ, VM_DEV_R_I32, 1, VM_DEV_F_U8),
};
static const vm_dev_contract_t k_contract = {.ops = k_ops, .count = ARRAY_LEN(k_ops), .naddr = 1};

bool vm_verify_hbridge(vm_block_h b) {
  return vm_device_check(b, &k_contract);
}

void vm_blk_hbridge(vm_block_h b) {
  vm_device_run(b, &k_contract);
}

VM_BLOCK_TYPE_DEFINE(VM_BLK_HBRIDGE, .run = vm_blk_hbridge, .check = vm_verify_hbridge, .min_in = 0,
                     .max_in = VM_DEV_FIELDS_MAX, .min_q = 0, .max_q = 1, .state_len = sizeof(vm_block_device_data_t));
