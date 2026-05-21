#include "rik_status_handler.h"
#include "rtos_utils.h"

R_TASK_DEFINE(rik_status_handler_task, 4096);
R_QUEUE_DEFINE(status_queue, 20, sizeof(status_rep_t));


TaskHandle_t _supervisor_task_handle = NULL;
RingbufHandle_t _status_buffer = NULL;

#define SEVERITY_CRITICAL 2

void status_handler_task(void* params) {
    status_rep_t current_report;

    while (1) {
        if (xQueueReceive(status_queue, &current_report, portMAX_DELAY) == pdTRUE) {

            ESP_LOGE("STATUS", "Critical error! Code: %lu, Owner: %lu, Origin Info: %lu", 
                     current_report.e_code, 
                     current_report.e_owner, 
                     current_report.track.origin_info);
            
            if (_supervisor_task_handle != NULL) {
                R_NOTIFY_SEND(_supervisor_task_handle, 0);
            }
        }
    }
}

void rik_status_handler_start(RingbufHandle_t status_buffer, TaskHandle_t supervisor_task_handle){
    _status_buffer = status_buffer;
    _supervisor_task_handle = supervisor_task_handle;

    status_assign_buffer(status_buffer, status_queue);
    R_TASK_START_ON_CORE(rik_status_handler_task, status_handler_task, NULL, 5, 0);
}