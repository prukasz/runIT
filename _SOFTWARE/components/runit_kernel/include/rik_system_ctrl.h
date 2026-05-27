#pragma once 
#include "status.h"

void rik_devices_freeze();
void rik_devices_unfreeze();

status_rep_t stop_devices(void);

status_rep_t handle_vm_stop(void);

void rik_callback_current_monitor(void* param);
void rik_callback_vreg(void* param);


typedef enum{
    SYS_CTRL_AUTOMATIC = 0x00,  //<- Predefined
    SYS_CTRL_SKIP_EVENT = 0x01, //<- Ignore 
    SYS_CTRL_VM_CALLBACKS_ONLY = 0x02, //<- Trigger vm (coed etc)
    SYS_CTRL_STOP = 0x03,  //<- Stop devices
    SYS_CTRL_ENTER_EMERGENCY = 0x04, //<- Stop devices + trigger emergency event (reset all devices, stop vm, etc)
    SYS_CTRL_DISABLE_DEVICE = 0x05, //<- Disable specific device (if possible)
}rik_sys_ctrl_flags_e;

typedef struct __attribute__((packed)) rik_sys_ctrl_cfg_t {
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
}rik_sys_ctrl_cfg_t;