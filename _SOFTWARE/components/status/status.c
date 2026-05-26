#include "status.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "rtos_utils.h"

status_manager_log_cfg _status_log_flags = {0};
RingbufHandle_t _status_buffer_handle = NULL;
QueueHandle_t _status_queue_handle = NULL;


R_MUTEX_DEFINE(status_lock)

void status_assign_buffer(RingbufHandle_t status_buffer, QueueHandle_t status_queue) {
    _status_buffer_handle = status_buffer;
    _status_queue_handle = status_queue;
    _status_log_flags = (status_manager_log_cfg){
        .rep_i = 1,
        .rep_w = 1,
        .rep_c = 1,
    };
}


void _sta_push_overwrite(const status_rep_t *_sta_err) {
    uint8_t severity = _sta_err->details.severity;
    bool should_add = false;

    switch (severity) {
        case 0: should_add = _status_log_flags.rep_i; break;
        case 1: should_add = _status_log_flags.rep_w; break;
        case 2: should_add = _status_log_flags.rep_c; break;
        default: break; 
    }

    if (severity >= 1 && _status_queue_handle != NULL) {
        // Try to send to the front (top) of the queue.
        while (xQueueSendToFront(_status_queue_handle, _sta_err, 0) != pdTRUE) {
            status_rep_t dummy;
            if (xQueueReceive(_status_queue_handle, &dummy, 0) != pdTRUE) {
                break; 
            }
        }
    }

    if (should_add && R_MUTEX_LOCK(status_lock, MSEC(10)) && _status_buffer_handle != NULL) {
        while (xRingbufferSend(_status_buffer_handle, _sta_err, sizeof(*_sta_err), 0) != pdTRUE) {
            size_t old_size = 0;
            void *old_item = xRingbufferReceive(_status_buffer_handle, &old_size, 0);
            if (old_item != NULL) {
                vRingbufferReturnItem(_status_buffer_handle, old_item);
            } else {
                break; 
            }
        }
        R_MUTEX_UNLOCK(status_lock);
    }
}

void status_mutex_lock() {
    R_MUTEX_LOCK(status_lock, portMAX_DELAY);
}
void status_mutex_unlock() {
    R_MUTEX_UNLOCK(status_lock);
}



