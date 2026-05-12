#include "status.h"
#include "rik_shared.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "rtos_utils.h"

#define SCHEDULER_TASK_STACK_SIZE 4096  
#define SCHEDULER_TASK_PRIORITY 5

void rik_scheduler_init(void* args){
    while (1)
    {   
        vTaskDelay(MSEC(1000)); // Delay for 1 second
    } 
}

R_TASK_DEFINE(rik_scheduler_task, SCHEDULER_TASK_STACK_SIZE);

void rik_scheduler_start() {
    R_TASK_START_ON_CORE(rik_scheduler_task, rik_scheduler_init, NULL, SCHEDULER_TASK_PRIORITY, 0);
}

TaskHandle_t rik_scheduler_get_task_handle() {
    return rik_scheduler_task;
}