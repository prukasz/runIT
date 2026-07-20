#include "sys_error_handler.h"
#include <stdint.h>
#include <stdio.h>
#include "error_dispatch.h"
#include "esp_log.h"
#include "status_codes.h"
#include "sys_ble.h"
#include "utils.h"

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

void sys_log_set_level(esp_log_level_t level) {
  esp_log_level_set("*", level);
}

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
      if (err.details.payload_type == STATUS_PAYLOAD_DEV_SOLO) {
        uint8_t dev_id = DEV_ERR_GET_DEV(err.payload);
        if (err.e_code == ERR_DEV_ALREADY_EXISTS) {
          ESP_LOGE("SYS_ERR", "%s: Device with ID %u already exists in the system registry.",
                   status_owner_to_name(err.e_owner), dev_id);
        } else if (err.e_code == ERR_DEV_NOT_FOUND) {
          ESP_LOGE("SYS_ERR", "%s: Failed to identify device with id %u, ensure that device is created and installed.",
                   status_owner_to_name(err.e_owner), dev_id);
        } else if (err.e_code == ERR_DEV_MISSING_HANDLE) {
          ESP_LOGE("SYS_ERR", "%s: Missing device handle for device ID %u.",
                   status_owner_to_name(err.e_owner), dev_id);
        } else if (err.e_code == ERR_DEV_SUSPENDED) {
          ESP_LOGE("SYS_ERR", "%s: Device with ID %u is suspended.",
                   status_owner_to_name(err.e_owner), dev_id);
        } else if (err.e_code == ERR_DEV_NOT_INSTALLED) {
          ESP_LOGE("SYS_ERR", "%s: Device with ID %u is registered but not installed.",
                   status_owner_to_name(err.e_owner), dev_id);
        } else if (err.e_code == ERR_I2C_DEV_NOT_FOUND) {
          ESP_LOGE("SYS_ERR", "%s: Device with ID %u was not found on the I2C bus (no ACK).",
                   status_owner_to_name(err.e_owner), dev_id);
        } else {
          ESP_LOGE("SYS_ERR", "%s: Device solo error %s (0x%04lx) on device ID %u, payload: %llu",
                   status_owner_to_name(err.e_owner), status_error_to_name(err.e_code), (unsigned long)err.e_code, dev_id, (unsigned long long)err.payload);
        }
      } else if (err.details.payload_type == STATUS_PAYLOAD_DEV_ESP) {
        uint8_t dev_id = DEV_ERR_GET_DEV(err.payload);
        uint32_t esp_err = DEV_ERR_GET_CODE(err.payload);
        if (err.e_code == ERR_DEV_DRIVER_ERR) {
          ESP_LOGE("SYS_ERR", "%s: ESP driver error on device ID %u: %s (0x%X)",
                   status_owner_to_name(err.e_owner), dev_id, esp_err_to_name(esp_err), (unsigned int)esp_err);
        } else if (err.e_code == ERR_DEV_FEATURE_NOT_FOUND) {
          uint8_t contract = (uint8_t)esp_err;
          ESP_LOGE("SYS_ERR", "%s: Device with ID %u does not support contract type %d.",
                   status_owner_to_name(err.e_owner), dev_id, contract);
        } else {
          ESP_LOGE("SYS_ERR", "%s: Device ESP error %s (0x%04lx) on device ID %u, ESP code: %s (0x%X)",
                   status_owner_to_name(err.e_owner), status_error_to_name(err.e_code), (unsigned long)err.e_code, dev_id, esp_err_to_name(esp_err), (unsigned int)esp_err);
        }
      } else if (err.details.payload_type == STATUS_PAYLOAD_DEV_DEP) {
        uint8_t dev_id = DEV_ERR_GET_DEV(err.payload);
        uint8_t dep_dev_id = DEV_ERR_GET_DEP(err.payload);
        uint32_t sta_err_code = DEV_ERR_GET_CODE(err.payload);
        if (err.e_code == ERR_DEV_DEP_ERR) {
          ESP_LOGE("SYS_ERR", "%s: Device with ID %u encountered a driver dependency failure on device ID %u: %s (0x%X).",
                   status_owner_to_name(err.e_owner), dev_id, dep_dev_id,
                   status_error_to_name(sta_err_code), (unsigned int)sta_err_code);
        } else {
          ESP_LOGE("SYS_ERR", "%s: Device dep error %s (0x%04lx) on device ID %u, dep device: %u, code: %s (0x%X)",
                   status_owner_to_name(err.e_owner), status_error_to_name(err.e_code), (unsigned long)err.e_code, dev_id, dep_dev_id, status_error_to_name(sta_err_code), (unsigned int)sta_err_code);
        }
      } else if (err.details.payload_type == STATUS_PAYLOAD_DEV_IO_ERR) {
        uint8_t dev_id = STA_PAYLOAD_GET_DEV_IO_ID(err.payload);
        uint8_t pin = STA_PAYLOAD_GET_DEV_IO_PIN(err.payload);
        uint32_t extra = STA_PAYLOAD_GET_DEV_IO_EXTRA(err.payload);
        ESP_LOGE("SYS_ERR", "%s: IO error %s (0x%04lx) on device ID %u, pin %u, extra %lu",
                 status_owner_to_name(err.e_owner), status_error_to_name(err.e_code), (unsigned long)err.e_code, dev_id, pin, (unsigned long)extra);
      } else {
        ESP_LOGW("SYS_ERR", "Handled status error: Owner=%s (0x%04lx), Code=%s (0x%04lx), Severity=%d, Payload=%llu",
                 status_owner_to_name(err.e_owner), (unsigned long)err.e_owner, status_error_to_name(err.e_code), (unsigned long)err.e_code, err.details.severity, (unsigned long long)err.payload);
      }
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
