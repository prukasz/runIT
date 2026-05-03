#include "status.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "esp_log.h"

status_manager_flags_t s_err_manager_flags = {0};
RingbufHandle_t s_err_buffer_handle = NULL;
RingbufHandle_t s_status_buffer_handle = NULL;
TaskHandle_t s_manager_task_handle = NULL;

static uint32_t s_set_bits = 0;
static uint32_t s_wait_bits = 0;

static m_status_cfg_t *s_manager_events;

void s_manager_cgf_i(bool en_log, bool en_rep) {
    s_err_manager_flags.log_i = en_log;
    s_err_manager_flags.rep_i = en_rep;
}

void s_manager_cgf_w(bool en_log, bool en_rep) {
    s_err_manager_flags.log_w = en_log;
    s_err_manager_flags.rep_w = en_rep;
}

void s_manager_cgf_c(bool en_log, bool en_rep) {
    s_err_manager_flags.log_c = en_log;
    s_err_manager_flags.rep_c = en_rep;
}


static void s_manager_task(void *pvParameters) {

    while (1) {
        xEventGroupWaitBits(s_manager_events->events, s_manager_events->bits_task_run, pdTRUE, pdFALSE, portMAX_DELAY);
        
        ESP_LOGI("s_manager_task", "Event received, processing...");

        xEventGroupSetBits(s_manager_events->events, s_manager_events->bits_task_done);
    }

}

void s_manager_init(m_status_cfg_t* events, RingbufHandle_t err_buffer, RingbufHandle_t status_buffer) {
    s_manager_events = events;
    s_err_buffer_handle = err_buffer;
    xTaskCreate(&s_manager_task, "s_manager_task", s_manager_events->task_stack_size, NULL, s_manager_events->task_priority, &s_manager_task_handle);
}


TaskHandle_t s_manager_get_task_handle(void) {
    return s_manager_task_handle;
}

RingbufHandle_t s_manager_get_buffer_handle(void) {
    return s_err_buffer_handle;
}



