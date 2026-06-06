#pragma once 
#include "status.h"

/************** USER AVIABLE *********************/
//stop update of devices
void sys_freeze();
//resume update of devices, and wait till update of pending is done 
void sys_unfreeze();

//disable peripherials
void sys_stop_or_resume(bool stop, bool reset_prev_state);
#define SYS_STOP() sys_stop_or_resume(1, 0);
//resume peripherials
#define SYS_RESUME() sys_stop_or_resume(0, 0);
void sys_devices_default_config(void);
/************** USER AVIABLE *********************/

status_rep_t handle_vm_stop(void);
void rik_callback_manager_pwr(void* param);
void rik_callback_adc(void* param);
void rik_callback_gpio(void* param);

/* Registration API for VM system callbacks. Kernel will invoke these when events occur.
 * VM component should register its handlers via these setters to avoid direct symbol
 * dependencies between the kernel and VM components.
 */
typedef void (*vm_sys_cb_t)(void *arg);
void rik_register_vm_sys_power(vm_sys_cb_t cb);
void rik_register_vm_sys_adc(vm_sys_cb_t cb);
void rik_register_vm_sys_gpio(vm_sys_cb_t cb);

typedef enum{
    SYS_CTRL_AUTOMATIC = 0x00,  //<- Predefined
    SYS_CTRL_SKIP_EVENT = 0x01, //<- Ignore 
    SYS_CTRL_VM_CALLBACKS_ONLY = 0x02, //<- Trigger vm (coed etc)
    SYS_CTRL_STOP = 0x03,  //<- Stop devices
    SYS_CTRL_ENTER_EMERGENCY = 0x04, //<- Stop devices + trigger emergency event (reset all devices, stop vm, etc)
    SYS_CTRL_DISABLE_DEVICE = 0x05, //<- Disable specific device (if possible)
}rik_sys_ctrl_flags_e;

typedef struct __attribute__((packed)) rik_sys_ctrl_power_cfg_t {
    uint8_t crt_reg0_ovp;
    uint8_t crt_reg0_ocp;
    uint8_t crt_reg0_scp;
    uint8_t crt_reg1_ovp;
    uint8_t crt_reg1_ocp;
    uint8_t crt_reg1_scp;
    uint8_t crt_current_REG0_WARN;
    uint8_t crt_current_REG0_CRIT;
    uint8_t crt_current_REG1_WARN;
    uint8_t crt_current_REG1_CRIT;
    uint8_t crt_current_SYS_PWR_WARN;
    uint8_t crt_current_SYS_PWR_CRIT;
}rik_sys_ctrl_power_cfg_t;


rik_sys_ctrl_power_cfg_t* sys_ctrl_get_power_cfg(void);
