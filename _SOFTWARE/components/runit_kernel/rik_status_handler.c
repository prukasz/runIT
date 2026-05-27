#include "rik_status_handler.h"
#include "rtos_utils.h"
#include "manager_io.h"
#include "rik_shared.h"
#include "status.h"
#include "rik_system_ctrl.h"

#define TAG __FILE_NAME__

R_TASK_DEFINE(rik_status_handler_task, 4096);
R_QUEUE_DEFINE(status_queue, 20, sizeof(status_rep_t));


TaskHandle_t _supervisor_task_handle = NULL;
RingbufHandle_t _status_buffer = NULL;

#define SEVERITY_CRITICAL 2


static void handle_manager_io_errors(status_rep_t* status){
    switch (status->e_code){
        case IO_ERR_PORT_INVALID:{
            handle_vm_stop();
            break;
        }
        case IO_ERR_FEATURE_UNSUPPORTED:{
            handle_vm_stop();
            break;
        }
        case IO_ERR_PIN_PROTECTED:{
            handle_vm_stop();
            stop_devices();
            break;
        }
        case IO_ERR_PIN_UNSUPPORTED:{
            handle_vm_stop();
            break;
        }
        case IO_ERR_MODE_UNSUPPORTED:{
            handle_vm_stop();
            break;
        }
        case IO_ERR_UPDATE_FAILED:{
            handle_vm_stop();
            stop_devices();
            break;
        }
        case IO_ERR_PIN_IN_OTHER_USE:{
            handle_vm_stop();
            break;
        }
        case IO_ERR_PIN_NOT_CONFIGURED: {
            handle_vm_stop();
            break;
        }
        default:{
            break;
        }
    }
}

static void handle_manager_pwr_errors(status_rep_t* status){
    switch (status->e_code){
        case PWR_ERR_DEVICE_NOT_FOUND:{
            handle_vm_stop();
            break;
        }
        case PWR_ERR_INVALID_PARAM:{
            handle_vm_stop();
            break;
        }
        case PWR_ERR_FEATURE_UNSUPPORTED:{
            handle_vm_stop();
            break;
        }
        case PWR_ERR_FEATURE_PROTECTED:{
            handle_vm_stop();
            stop_devices();
            break;
        }
        case PWR_ERR_MODE_UNSUPPORTED:{
            handle_vm_stop();
            break;
        }
        case PWR_ERR_UPDATE_FAILED:{
            handle_vm_stop();
            stop_devices();
            break;
        }
    }
}



static void status_handler_task(void* params) {
    status_rep_t current_report;

    while (1) {
        if (xQueueReceive(status_queue, &current_report, portMAX_DELAY) == pdTRUE) {

            ESP_LOGE(
                "STATUS",
                "Error! Owner: %s (0x%04" PRIX32 "), Code: %s (0x%04" PRIX32 "), Origin Info: %" PRIu64,
                status_owner_to_name(current_report.e_owner),
                current_report.e_owner,
                status_error_to_name(current_report.e_code),
                current_report.e_code,
                (uint64_t)current_report.track.origin_info
            );
            uint32_t owner_type = current_report.e_owner & 0xFF00;
            if (owner_type == OWNER_IO_MANAGER) {
                handle_manager_io_errors(&current_report);
            }else if (owner_type == OWNER_MANAGER_PWR) {
                handle_manager_pwr_errors(&current_report);
            }

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