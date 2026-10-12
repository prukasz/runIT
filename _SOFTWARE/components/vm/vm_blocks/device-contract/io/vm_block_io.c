#include "vm_block_io.h"
#include "sys_io.h"
#include "vm_blocks.h"

/* The table: each row's fields, in the order of the contract's argument struct. A _Static_assert keeps them the size
 * of the structs the device parses. */
_Static_assert(sizeof(io_args_SET_LEVEL_t) == 2 && sizeof(io_args_TOGGLE_t) == 1 && sizeof(io_args_GET_LEVEL_t) == 1 &&
                   sizeof(io_args_SET_PWM_DUTY_t) == 5 && sizeof(io_args_SET_PWM_FREQ_t) == 5 &&
                   sizeof(io_args_SET_MODE_t) == 2 && sizeof(io_args_RESET_t) == 1 &&
                   sizeof(io_args_GET_VOLTAGE_t) == 1 && sizeof(io_args_SET_VOLTAGE_t) == 5,
               "an IO op's argument struct changed: update the table of the IO block");

static const vm_dev_op_t k_ops[] = {
    //@op IO_OP_SET_LEVEL @alias Set level @description Drives a pin high or low. @face {pin} = {level}
    //@op-in pin @alias Pin @id pin
    //@op-in level @alias Level @choices 0=Low,1=High
    VM_DEV_OP(IO_OP_SET_LEVEL, VM_DEV_ACT_CHANGE, VM_DEV_R_NONE, 2, VM_DEV_F_U8, VM_DEV_F_U8),
    //@op IO_OP_TOGGLE @alias Toggle @description Flips the level of a pin once on each rising enable. @face Toggle {pin}
    //@op-in pin @alias Pin @id pin
    VM_DEV_OP(IO_OP_TOGGLE, VM_DEV_ACT_EDGE, VM_DEV_R_NONE, 1, VM_DEV_F_U8),
    //@op IO_OP_GET_LEVEL @alias Read level @description Reads the level of a pin. @face Read {pin}
    //@op-in pin @alias Pin @id pin
    //@op-out level @alias Level @choices 0=Low,1=High
    VM_DEV_OP(IO_OP_GET_LEVEL, VM_DEV_ACT_READ, VM_DEV_R_U8, 1, VM_DEV_F_U8),
    //@op IO_OP_SET_PWM_DUTY @alias Set PWM duty @description Sets the PWM duty of a pin. @face {pin} duty {duty}
    //@op-in pin @alias Pin @id pin
    //@op-in duty @alias Duty @unit steps @min 0 @max 4095
    VM_DEV_OP(IO_OP_SET_PWM_DUTY, VM_DEV_ACT_CHANGE, VM_DEV_R_NONE, 2, VM_DEV_F_U8, VM_DEV_F_U32),
    //@op IO_OP_SET_PWM_FREQ @alias Set PWM frequency @description Sets the PWM frequency of a pin (a device with one frequency for all its pins ignores the pin). @face {pin} at {frequency}
    //@op-in pin @alias Pin @id pin
    //@op-in frequency @alias Frequency @unit Hz
    VM_DEV_OP(IO_OP_SET_PWM_FREQ, VM_DEV_ACT_CHANGE, VM_DEV_R_NONE, 2, VM_DEV_F_U8, VM_DEV_F_U32),
    //@op IO_OP_SET_MODE @alias Set mode @description Configures what a pin does. @face {pin} as {mode}
    //@op-in pin @alias Pin @id pin
    //@op-in mode @alias Mode @choices 0=Input,1=Input_pull-up,2=Input_pull-down,3=Output,4=Open_drain,5=Open_drain_pull-up,6=PWM,7=ADC,8=DAC
    VM_DEV_OP(IO_OP_SET_MODE, VM_DEV_ACT_CHANGE, VM_DEV_R_NONE, 2, VM_DEV_F_U8, VM_DEV_F_U8),
    //@op IO_OP_RESET @alias Reset pin @description Puts a pin back in its power-on state once on each rising enable. @face Reset {pin}
    //@op-in pin @alias Pin @id pin
    VM_DEV_OP(IO_OP_RESET, VM_DEV_ACT_EDGE, VM_DEV_R_NONE, 1, VM_DEV_F_U8),
    //@op IO_OP_GET_VOLTAGE @alias Read voltage @description Reads the voltage of an analog pin. @face Read {pin}
    //@op-in pin @alias Pin @id pin
    //@op-out voltage @alias Voltage @unit mV
    VM_DEV_OP(IO_OP_GET_VOLTAGE, VM_DEV_ACT_READ, VM_DEV_R_I32, 1, VM_DEV_F_U8),
    //@op IO_OP_SET_VOLTAGE @alias Set voltage @description Drives an analog output pin to a voltage. @face {pin} = {voltage}
    //@op-in pin @alias Pin @id pin
    //@op-in voltage @alias Voltage @unit mV
    VM_DEV_OP(IO_OP_SET_VOLTAGE, VM_DEV_ACT_CHANGE, VM_DEV_R_NONE, 2, VM_DEV_F_U8, VM_DEV_F_I32),
};
static const vm_dev_contract_t k_contract = {.ops = k_ops, .count = ARRAY_LEN(k_ops), .naddr = 1};

bool vm_verify_io(vm_block_h b) {
  return vm_device_check(b, &k_contract);
}

void vm_blk_io(vm_block_h b) {
  vm_device_run(b, &k_contract);
}

VM_BLOCK_TYPE_DEFINE(VM_BLK_IO, .run = vm_blk_io, .check = vm_verify_io, .min_in = 0, .max_in = VM_DEV_FIELDS_MAX,
                     .min_q = 0, .max_q = 1, .state_len = sizeof(vm_block_device_data_t));
