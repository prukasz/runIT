#include "status.h"
#include <stdint.h>
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sys_ble.h"
#include "utils.h"

static struct {
  uint8_t rep_level;
  uint8_t last_rep_level;
  uint8_t status_buffer_id;
  uint16_t status_char_uuid;
  QueueHandle_t status_queue_handle;
} status_config;

void status_assign_error_tx(uint16_t char_uuid, uint8_t buffer_id, QueueHandle_t status_queue) {
  status_config.status_char_uuid = char_uuid;
  status_config.status_buffer_id = buffer_id;
  status_config.status_queue_handle = status_queue;
}

void status_set_rep_mode(uint8_t rep_level) {
  status_config.rep_level = rep_level;
  status_config.last_rep_level = rep_level;
}

void status_suspend() {
  status_config.last_rep_level = status_config.rep_level;
  status_config.rep_level = 0xFF;  // set to rep none
}
void status_resume() { status_config.rep_level = status_config.last_rep_level; }

void _sta_push(const status_rep_t* _sta_err) {
  if (_sta_err->details.severity >= status_config.rep_level && status_config.status_queue_handle != NULL) {
    while (xQueueSendToFront(status_config.status_queue_handle, _sta_err, 0) != pdTRUE) {
      status_rep_t dummy;
      if (xQueueReceive(status_config.status_queue_handle, &dummy, 0) != pdTRUE) {
        break;
      }
    }

    if (status_config.status_char_uuid != 0) {
      status_suspend();  // avoid circular errors
      sys_ble_char_send(status_config.status_char_uuid, status_config.status_buffer_id, (const uint8_t*)_sta_err, sizeof(status_rep_t), true);
      status_resume();
    }
  }
}
