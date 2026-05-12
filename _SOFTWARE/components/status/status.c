#include "status.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "esp_log.h"

status_manager_log_cfg _status_log_flags = {0};
RingbufHandle_t _status_buffer_handle = NULL;
static bool _connection_established = false;
static StaticSemaphore_t _status_mutex_buffer;
SemaphoreHandle_t _status_mutex = NULL;

void status_manager_init(RingbufHandle_t status_buffer) {
    _status_buffer_handle = status_buffer;
    _status_log_flags = (status_manager_log_cfg){
        .log_i = 1,
        .log_w = 1,
        .log_c = 1,
        .rep_i = 1,
        .rep_w = 1,
        .rep_c = 1,
    };
    _status_mutex = xSemaphoreCreateMutexStatic(&_status_mutex_buffer);
}


void _sta_push_overwrite(const status_rep_t *_sta_err) {
    if (!_connection_established) { return; }

    uint8_t severity = _sta_err->details.severity;
    bool should_log = false;

    switch (severity) {
        case 0: should_log = _status_log_flags.rep_i; break;
        case 1: should_log = _status_log_flags.rep_w; break;
        case 2: should_log = _status_log_flags.rep_c; break;
        default: break; 
    }

    if (should_log && xSemaphoreTake(_status_mutex, pdMS_TO_TICKS(10)) == pdTRUE ) {
        while (xRingbufferSend(_status_buffer_handle, _sta_err, sizeof(*_sta_err), 0) != pdTRUE) {
            size_t old_size = 0;
            void *old_item = xRingbufferReceive(_status_buffer_handle, &old_size, 0);
            if (old_item != NULL) {
                vRingbufferReturnItem(_status_buffer_handle, old_item);
            } else {
                break; 
            }
        }
        xSemaphoreGive(_status_mutex);
    }
}

void _sta_log_and_push(const status_rep_t *err, const char *file, const char *func, const int line, const char *fmt, ...) {
    if (err->e_code == 0) return;

    _sta_push_overwrite(err);

    bool should_log = false;
    char log_type = 'I'; 

    if (err->details.severity == 0 && _status_log_flags.log_i) {
        should_log = true; log_type = 'I';
    } else if (err->details.severity == 1 && _status_log_flags.log_w) {
        should_log = true; log_type = 'W';
    } else if (err->details.severity == 2 && _status_log_flags.log_c) {
        should_log = true; log_type = 'E';
    }

    if (should_log) {
        va_list args;
        va_start(args, fmt);
        
        esp_log_write(log_type == 'E' ? ESP_LOG_ERROR : (log_type == 'W' ? ESP_LOG_WARN : ESP_LOG_INFO), 
                      file, "[%s:%d] ", func, line);
        vprintf(fmt, args);
        printf("\n");
        va_end(args);
    }
}


/**
 * @brief Mark if connection is established
 */
void status_manager_connection_update(bool connected) {
    _connection_established = connected;
}

