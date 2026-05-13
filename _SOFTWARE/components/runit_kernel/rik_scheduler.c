#include "status.h"
#include "rik_shared.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "rtos_utils.h"
#include "rik_logs.h"
#include "rik_modules.h"

#define SCHEDULER_TASK_STACK_SIZE 4096  
#define SCHEDULER_TASK_PRIORITY 5

#define TAG __FILE_NAME__

void rik_scheduler(void* args){
    while (1)
    {   
        R_NOTIFY_AWAIT(WAIT_FOREVER, NULL); 
        EventBits_t current_bits = xEventGroupGetBits(rik_events_communication);
        if(current_bits & EVENT_BIT_BLE_CONNECTED){
            ESP_LOGI(TAG, "BLE Connected event received");
            _rik_ble_active = true;
            rik_start_interface(rik_events_communication); // Initialize BLE-related interrupts after BLE is connected to avoid spurious events during startup
            rik_log_remote_enable(true);

            R_EVENT_CLEAR(rik_events_communication, EVENT_BIT_BLE_CONNECTED);
        }else if (current_bits & EVENT_BIT_BLE_CONNECTION_FAILED)
        {
            ESP_LOGI(TAG, "BLE Connection Failed event received");
                    _rik_ble_active = false;
            R_EVENT_CLEAR(rik_events_communication, EVENT_BIT_BLE_CONNECTION_FAILED);
        }else if (current_bits & EVENT_BIT_BLE_DISCONNECTED)
        {
            ESP_LOGI(TAG, "BLE Disconnected event received");
            _rik_ble_active = false;
            R_EVENT_CLEAR(rik_events_communication, EVENT_BIT_BLE_DISCONNECTED);
        }
        
    
    } 
}

R_TASK_DEFINE(rik_scheduler_task, SCHEDULER_TASK_STACK_SIZE);

void rik_scheduler_start() {
    R_TASK_START_ON_CORE(rik_scheduler_task, rik_scheduler, NULL, SCHEDULER_TASK_PRIORITY, 0);
}

TaskHandle_t rik_scheduler_get_task_handle() {
    return rik_scheduler_task;
}