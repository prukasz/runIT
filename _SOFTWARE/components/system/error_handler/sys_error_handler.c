#include "sys_error_handler.h"
#include <stdio.h>
#include "esp_log.h"
#include "sys_ble.h"
#include "utils.h"
#include "error_dispatch.h"

static uint16_t s_log_char_uuid = 0;
static uint8_t s_log_buffer_id = 0;
static bool s_mirror_on_serial = true;
static bool s_errors_suspended = false;

R_QUEUE_DEFINE(s_error_queue, 10, sizeof(status_rep_t));
R_TASK_DEFINE(s_error_task_handle, 4096);

int sys_log_vprintf(const char* fmt, va_list args) {
  char buf[512];
  va_list args_copy;
  va_copy(args_copy, args);
  int len = vsnprintf(buf, sizeof(buf), fmt, args_copy);
  va_end(args_copy);

  if (len > 0 && s_log_char_uuid != 0) {
    sys_ble_char_send(s_log_char_uuid, s_log_buffer_id, (const uint8_t*)buf, len, true);
  }

  if (s_mirror_on_serial) {
    return vprintf(fmt, args);
  }
  return 0;
}

void sys_log_set_level(esp_log_level_t level) { esp_log_level_set("*", level); }

void sys_log_assign_ble_tx(uint16_t char_uuid, uint8_t buffer_id, bool mirror_on_serial) {
  s_log_char_uuid = char_uuid;
  s_log_buffer_id = buffer_id;
  s_mirror_on_serial = mirror_on_serial;
}

void sys_error_supress() {
  s_errors_suspended = true;
  status_suspend();
}

void sys_error_resume() {
  s_errors_suspended = false;
  status_resume();
}

void sys_error_stream_config(bool stream_info, bool stream_warn, bool stream_crit) {
  if (stream_info) {
    status_set_rep_mode(STATUS_INFO);
  } else if (stream_warn) {
    status_set_rep_mode(STATUS_WARNING);
  } else if (stream_crit) {
    status_set_rep_mode(STATUS_CRITICAL);
  } else {
    status_set_rep_mode(0xFF);
  }
}

static void sys_error_handler_task(void* arg) {
  status_rep_t err;
  while (1) {
    if (xQueueReceive(s_error_queue, &err, portMAX_DELAY) == pdTRUE) {
      if (s_errors_suspended) {
        continue;
      }
      // Log the handled error
      ESP_LOGW("SYS_ERR", "Handled status error: Owner=0x%04lx, Code=0x%04lx, Severity=%d", err.e_owner, err.e_code, err.details.severity);
      // Dispatch error to registered device error handlers
      sys_error_switch(&err);
    }
  }
}

void sys_error_handler_init(uint16_t error_char_uuid, uint8_t error_buffer_id, uint16_t log_char_uuid, uint8_t log_buffer_id) {
  status_assign_error_tx(error_char_uuid, error_buffer_id, s_error_queue);

  sys_log_assign_ble_tx(log_char_uuid, log_buffer_id, true);
  esp_log_set_vprintf(sys_log_vprintf);

  R_TASK_START(s_error_task_handle, sys_error_handler_task, NULL, 5);
}