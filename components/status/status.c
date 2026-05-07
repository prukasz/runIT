#include "status.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "esp_log.h"


status_manager_log_cfg _status_log_flags = {0};
RingbufHandle_t _status_buffer_handle = NULL;

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


void status_manager_init(RingbufHandle_t status_buffer) {
    _status_buffer_handle = status_buffer;
    status_manager_cgf_i(true, true);
    status_manager_cgf_w(true, true);
    status_manager_cgf_c(true, true);
}




