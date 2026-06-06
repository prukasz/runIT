#pragma once 
#include "vm_code.h"
#include "status.h"


typedef enum{
    VM_CALLBACK_IDLE = 0,

    VM_CALLBACK_TIMER, 

    VM_SYS_CALLBACK_POWER_EVENT, /* manager_pwr_cb_type_e as arg */

    VM_SYS_CALLBACK_GPIO, /* sys_pin_t arg */
    VM_SYS_CALLBACK_ADC,  /* sys_pin_t arg */

    VM_SYS_CALLBACK_FAULT, /* none */

}vm_callback_types_t;

void vm_callback_sys_gpio(void *arg);
void vm_callback_sys_adc(void *arg);
void vm_callback_sys_power(void *arg);

/**
 * @brief add callback for section on given callback type and event match value
 */
status_rep_t vm_callback_section_add(vm_code_section_t *section, uint32_t event_type_match, vm_callback_types_t callback_type);

/**
 * @brief remove callback for section on given callback type and event match value
 */
status_rep_t vm_callback_section_remove(vm_code_section_t *section, uint32_t event_type_match, vm_callback_types_t callback_type);

