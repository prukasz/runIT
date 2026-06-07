#pragma once 
#include "status.h"
#include "rtos_utils.h"

#define EVENT_BIT_VM_WIRELESS_CONNECTION_PRESENT (1 << 0) // rik -> vm
#define EVENT_BIT_VM_READY          (1 << 1) // vm -> rik
#define EVENT_BIT_VM_OFFLINE_MODE   (1 << 2) // vm -> rik  invoked by remote
#define EVENT_BIT_VM_ONLINE_MODE    (1 << 3) // vm -> rik  invoked by remote
#define EVENT_BIT_VM_CMD_COMPLETE   (1 << 4) // vm -> rik
#define EVENT_BIT_VM_STOP           (1 << 9) // rik -> vm
#define EVENT_BIT_VM_EMERGENCY      (1 << 11) // rik -> vm
#define EVENT_BIT_VM_RESET          (1 << 12) // rik -> vm

typedef struct vm_config_t{
    EventGroupHandle_t vm_event_group;
    EventBits_t bit_vm_wireless_connection_present;
    EventBits_t bit_vm_offline_mode;
    EventBits_t bit_vm_online_mode;

    EventBits_t bit_vm_cmd_complete;
    EventBits_t bit_vm_ready;
    EventBits_t bit_vm_run;
    
    EventBits_t bit_vm_stop;
    EventBits_t bit_vm_emergency;
    EventBits_t bit_vm_reset;

    TaskHandle_t supervisor_task;
}vm_config_t;

status_rep_t vm_start(vm_config_t* config);
