#include "vm_main.h"
#include "string.h"
#include "vm_runner.h"
#include "rik_system_ctrl.h"
#include "vm_reset.h"
#define TAG __FILE_NAME__
vm_config_t my_config;

R_TASK_DEFINE(vm_task, 4096);
R_TASK_DEFINE(vm_runner_task, 4096);
R_BINARY_SEM_DEFINE(vm_section_run);
R_BINARY_SEM_DEFINE(vm_section_done);


static void vm_task_function(void* args){
    EventBits_t bits_to_wait = my_config.bit_vm_wireless_connection_present |my_config.bit_vm_stop|
    my_config.bit_vm_reset | my_config.bit_vm_emergency |my_config.bit_vm_run;
    xSemaphoreGive(vm_section_done);
    EventBits_t event = 0;
    while (1){
        event = xEventGroupWaitBits(my_config.vm_event_group, bits_to_wait , pdTRUE, pdFALSE, WAIT_FOREVER);
            if(event & my_config.bit_vm_wireless_connection_present){

            }else if(event & my_config.bit_vm_stop){
                
                xSemaphoreTake(vm_section_run, NO_WAIT);
                
            }else if(event & my_config.bit_vm_reset){
                xSemaphoreTake(vm_section_run, NO_WAIT);
                xSemaphoreTake(vm_section_done, MSEC(100));
                vm_reset();
                xSemaphoreGive(vm_section_done);
            }else if(event & my_config.bit_vm_emergency){

                xSemaphoreTake(vm_section_run, NO_WAIT);

            }else if(event & my_config.bit_vm_run){
                xSemaphoreGive(vm_section_done);
                xSemaphoreGive(vm_section_run);
            }
    }
}

static void vm_task_runner_function(void* args){
    while(1){
        xSemaphoreTake(vm_section_run, WAIT_FOREVER);
        xSemaphoreGive(vm_section_run);
        xSemaphoreTake(vm_section_done, WAIT_FOREVER);

        //freeze vm variables//code from remote update
       // vm_freeze_resources();
        //freeze io state
        sys_freeze();
        //run one section
        vm_code_runner(); 
        // unfreeze vm variables and update based on buffered packets
       // vm_unfreeze_resources();
        //unfreeze io state and update
        sys_unfreeze();  
        xSemaphoreGive(vm_section_done);
        vTaskDelay(MSEC(200));
    }
}


status_rep_t vm_start(vm_config_t* config){
    memcpy(&my_config, config, sizeof(vm_config_t));
    R_TASK_START_ON_CORE(vm_task, vm_task_function, NULL, 11, 1);
    R_TASK_START_ON_CORE(vm_runner_task, vm_task_runner_function, NULL, 10, 1);
    return STA_OK;
}