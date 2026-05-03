#include "status.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "esp_log.h"


status_manager_log_cfg _status_log_flags = {0};
RingbufHandle_t _status_buffer_handle = NULL;
TaskHandle_t _manager_task_handle = NULL;

static m_status_cfg_t *_cfg;

void status_manager_cgf_i(bool en_log, bool en_rep) {
    _status_log_flags.log_i = en_log;
    _status_log_flags.rep_i = en_rep;
}

void status_manager_cgf_w(bool en_log, bool en_rep) {
    _status_log_flags.log_w = en_log;
    _status_log_flags.rep_w = en_rep;
}

void status_manager_cgf_c(bool en_log, bool en_rep) {
    _status_log_flags.log_c = en_log;
    _status_log_flags.rep_c = en_rep;
}


static void s_manager_task(void *pvParameters) {

    while (1) {
        xEventGroupWaitBits(_cfg->events, _cfg->bits_task_run, pdTRUE, pdFALSE, portMAX_DELAY);
        
        ESP_LOGI("s_manager_task", "Event received, processing...");

        xEventGroupSetBits(_cfg->events, _cfg->bits_task_done);
    }

}

void status_manager_init(m_status_cfg_t* events, RingbufHandle_t status_buffer) {
    _cfg = events;
    _status_buffer_handle = status_buffer;
    xTaskCreate(&s_manager_task, "s_manager_task", _cfg->task_stack_size, NULL, _cfg->task_priority, &_manager_task_handle);
}

TaskHandle_t status_manager_get_task_handle(void) {
    return _manager_task_handle;
}




