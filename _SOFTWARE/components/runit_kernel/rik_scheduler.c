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

void process_wireless_events(){
    EventBits_t flags_wireless = xEventGroupGetBits(rik_events_wireless);
    EventBits_t flags_vm = xEventGroupGetBits(rik_events_vm);
    if ((flags_wireless & EVENT_BIT_BLE_CONNECTED) &&!_rik_ble_active) {
        _rik_ble_active = true;
        ESP_LOGI(TAG, "BLE Connected");
        xEventGroupClearBits(rik_events_wireless, EVENT_BIT_BLE_CONNECTION_FAILED | EVENT_BIT_BLE_DISCONNECTED);
        rik_log_remote_enable(true);

        // Allow data processing and tell vm 

        R_EVENT_SET(rik_events_vm, EVENT_BIT_VM_WIRELESS_CONNECTION_PRESENT);

    }
    if (flags_wireless & EVENT_BIT_BLE_DISCONNECTED && _rik_ble_active) {
        _rik_ble_active = false;
        ESP_LOGI(TAG, "BLE Disconnected");
        xEventGroupClearBits(rik_events_wireless, EVENT_BIT_BLE_CONNECTED);
        if (flags_vm & EVENT_BIT_VM_ONLINE_MODE) {
            //connection lost while in online mode 
            R_EVENT_SET(rik_events_vm, EVENT_BIT_VM_EMERGENCY);
        }

    }
    if (flags_wireless & EVENT_BIT_BLE_CONNECTION_FAILED && _rik_ble_active) {
        ESP_LOGI(TAG, "BLE Connection Failed");
        _rik_ble_active = false;
        xEventGroupClearBits(rik_events_wireless, EVENT_BIT_BLE_CONNECTED);
        R_EVENT_SET(rik_events_vm, EVENT_BIT_VM_EMERGENCY);

    }
    if (flags_wireless & EVENT_BIT_BLE_MTU_UPDATED) {
        ESP_LOGI(TAG, "BLE MTU Updated");
        xEventGroupClearBits(rik_events_wireless, EVENT_BIT_BLE_MTU_UPDATED);
    }

    if (flags_wireless & EVENT_BIT_BLE_ON_RX_FAILED) {
        ESP_LOGI(TAG, "BLE data receive failed");
        if (flags_vm & EVENT_BIT_VM_ONLINE_MODE) {
            R_EVENT_SET(rik_events_vm, EVENT_BIT_VM_EMERGENCY);
        }
        xEventGroupClearBits(rik_events_wireless, EVENT_BIT_BLE_ON_RX_FAILED);
    }
}




void rik_scheduler(void* args){
    uint32_t notification_value;
    while (1)
    {   
        if (R_NOTIFY_AWAIT(WAIT_FOREVER, &notification_value) == pdTRUE){
        process_wireless_events();
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