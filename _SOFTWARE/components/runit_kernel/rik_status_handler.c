#include "rik_status_handler.h"
#include "rtos_utils.h"
#include "manager_io.h"
#include "rik_shared.h"
#include "status.h"
#include "rik_system_ctrl.h"
#include "sdkconfig.h"

#define TAG __FILE_NAME__

R_TASK_DEFINE(rik_status_handler_task, 4096);
R_QUEUE_DEFINE(status_queue, CONFIG_MAX_PENDING_STATUS_REP, sizeof(status_rep_t));

TaskHandle_t _supervisor_task_handle = NULL;
RingbufHandle_t _status_buffer = NULL;

#define SEVERITY_CRITICAL 2


static void handle_manager_io_errors(status_rep_t* status){
    int64_t pin_info = status->track.origin_info;
    uint8_t port_id = SYS_IO_GET_PORT(pin_info);
    uint8_t pin_num = SYS_IO_GET_PIN(pin_info);
    uint8_t info_extra = SYS_IO_GET_INFO_EXTRA(pin_info);
    switch (status->e_code){
        case ERR_MISSING_HANDLE:{
            ESP_LOGW(TAG, "Missing handle for %s", status_owner_to_name(status->e_owner));
            break;
        }
        case IO_ERR_NO_FREE_PORT:{
            ESP_LOGE(TAG, "No free IO ports available, device didn't register");
            SYS_STOP();
            break;
        }
        case IO_ERR_FEATURE_UNSUPPORTED:{
            if (status->e_owner == OWNER_IO_PORT_CONFIGURE) {
                ESP_LOGW(TAG, "Sys unaviable to set mode %s on port %d, pin %d",sys_io_mode_to_string(info_extra), port_id, pin_num);
            }else{
                ESP_LOGW(TAG, "Sys feature unsupported %s  for port %d, pin %d", sys_io_feature_to_string(info_extra), port_id, pin_num);
            }
            break;
        }
        case ERR_INVALID_ARG:{
            ESP_LOGW(TAG, "Invalid parameter for IO operation: %s, Port: %d, Pin: %d, hex: %016llX", status_owner_to_name(status->e_owner), port_id, pin_num, status->track.origin_info);
            break;
        }
        case IO_ERR_PIN_PROTECTED:{
            ESP_LOGE(TAG, "Pin %d on port %d is protected", pin_num, port_id);
            handle_vm_stop();
            SYS_STOP();
            break;
        }
        case IO_ERR_PIN_UNSUPPORTED:{
            ESP_LOGW(TAG, "Unsupported pin %d, on port %d", pin_num, port_id);
            break;
        }
        case IO_ERR_MODE_UNSUPPORTED:{
            ESP_LOGE(TAG, "Unsupported mode %s, on port %d, pin %d", sys_io_mode_to_string(info_extra), port_id, pin_num);
            handle_vm_stop();
            SYS_STOP();
            break;
        }
        case IO_ERR_UPDATE_FAILED:{
            ESP_LOGE(TAG, "Failed to update pin %d on port %d", pin_num, port_id);
            handle_vm_stop();
            SYS_STOP();
            break;
        }
        case IO_ERR_PIN_IN_OTHER_USE:{
            ESP_LOGW(TAG, "Pin %d on port %d is already in use", pin_num, port_id);
            break;
        }
        case IO_ERR_PIN_NOT_CONFIGURED: {
            ESP_LOGW(TAG, "Pin %d on port %d not configured", pin_num, port_id);
            break;
        }
        default:{
            break;
        }
    }
}

static void handle_manager_pwr_errors(status_rep_t* status){
    switch (status->e_code){
        case ERR_MISSING_HANDLE:{
            ESP_LOGE(TAG, "Missing handle for %s, device isn't connected or initialization failed ", status_owner_to_name(status->e_owner));
            break;
        }
        case ERR_INVALID_ARG:{
            ESP_LOGW(TAG, "Invalid parameter for %s, device didn't apply setting. Param: %lld", status_owner_to_name(status->e_owner), status->track.origin_info);
            break;
        }
        case PWR_ERR_FEATURE_UNSUPPORTED:{
            ESP_LOGW(TAG, "Sys feature unsupported: %s, device may be disabled", status_owner_to_name(status->e_owner));
            break;
        }

        case PWR_ERR_UPDATE_FAILED:{
            ESP_LOGE(TAG, "Critical error: Failed to update %s", status_owner_to_name(status->e_owner));
            handle_vm_stop();
            break;
        }
        case PWE_ERR_PARSE_FAILED:{
            ESP_LOGW(TAG, "Failed to parse power config packet from %s. Original error code: %s", status_owner_to_name(status->e_owner), status_error_to_name(status->track.origin_info));
            break;
        }
        default:{
            ESP_LOGW(TAG, "unhandled error from power manager: %s %s", status_error_to_name(status->e_code), status_owner_to_name(status->e_owner));
            break;
        }
    }
}



static void status_handler_task(void* params) {
    status_rep_t current_report;

    while (1) {
        if (xQueueReceive(status_queue, &current_report, portMAX_DELAY) == pdTRUE) {
            if (current_report.e_code == ERR_ESP){
                //esp error losg 
                ESP_LOGE(TAG, "ESP error: %s, from %s", esp_err_to_name(current_report.track.origin_info), status_owner_to_name(current_report.e_owner));
                continue;
            }
            uint32_t owner_type = current_report.e_owner & 0xFF00;
        // 0xE200 -> IO Providers (Except Power Delivery 0xe217), 0xE900 -> Power Providers
        if (owner_type == OWNER_IO_MANAGER || (owner_type == 0xE200 && current_report.e_owner != OWNER_PROVIDER_POWER_DELIVERY)) {
                handle_manager_io_errors(&current_report);
        }else if (owner_type == OWNER_MANAGER_PWR || owner_type == 0xE900 || current_report.e_owner == OWNER_PROVIDER_POWER_DELIVERY) {
                handle_manager_pwr_errors(&current_report);
            } else {
                ESP_LOGW(TAG, "Unhandled status owner type: %s", status_owner_to_name(current_report.e_owner));
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
    R_TASK_START_ON_CORE(rik_status_handler_task, status_handler_task, NULL, 3, 0);
}