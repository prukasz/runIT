#include "status.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "esp_log.h"

status_manager_flags_t s_err_manager_flags = {0};
RingbufHandle_t s_err_buffer_handle = NULL;
TaskHandle_t s_manager_task_handle = NULL;
static EventGroupHandle_t s_events_to_set = NULL;
static EventGroupHandle_t s_events_to_wait = NULL;
static uint32_t s_set_bits = 0;
static uint32_t s_wait_bits = 0;

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
        xEventGroupWaitBits(s_events_to_wait, s_wait_bits, pdTRUE, pdFALSE, portMAX_DELAY);
        
        ESP_LOGI("s_manager_task", "Event received, processing...");

        xEventGroupSetBits(s_events_to_set, s_set_bits);
    }

}

void s_manager_init(EventGroupHandle_t events_to_set, EventGroupHandle_t events_to_wait, uint32_t set_bits, 
    uint32_t wait_bits, RingbufHandle_t buffer) {
    s_events_to_set = events_to_set;
    s_events_to_wait = events_to_wait;
    s_set_bits = set_bits;
    s_wait_bits = wait_bits;
    s_err_buffer_handle = buffer;
    xTaskCreate(&s_manager_task, "s_manager_task", 4096, NULL, 4, &s_manager_task_handle);
}


TaskHandle_t s_manager_get_task_handle(void) {
    return s_manager_task_handle;
}

RingbufHandle_t s_manager_get_buffer_handle(void) {
    return s_err_buffer_handle;
}



