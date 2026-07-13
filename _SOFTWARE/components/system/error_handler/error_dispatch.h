#pragma once
#include "status.h"
#include "sys_io.h"
#include "sys_power.h"
#include "sys_device.h"
#include "sys_error_handler.h"
#include "esp_log.h"

/**
 * @brief switch error based on owner, each owner base should provide own error handler
 */
static inline void sys_error_switch(status_rep_t* err) {
  if (!err) return;

  uint16_t owner_base = err->e_owner & 0xF000;
  switch (owner_base) {
    case 0xA000:
    case 0xD000:
    case 0xE000: {
      uint8_t device_id = 0xFF;
      if (err->details.payload_type == STATUS_PAYLOAD_DEVICE) {
        device_id = (uint8_t)(err->payload >> 32);
      } else if (err->details.payload_type == STATUS_PAYLOAD_SYS_IO) {
        device_id = (uint8_t)(err->payload >> 40);
      }

      if (device_id != 0xFF) {
        sys_device_t* dev = sys_device_get_by_id(device_id);
        if (dev) {
          // Suspend error reporting during error handling to prevent recursion
          sys_error_supress();

          if (dev->error_handler) {
            dev->error_handler(dev->device_handle, err);
          } else {
            // Dummy generic handler
            ESP_LOGW("SYS_ERR", "Generic fallback: resetting device %s (ID: %d)", dev->name, device_id);
            sys_device_reset(device_id);
          }

          // Resume error reporting
          sys_error_resume();
        }
      }
      break;
    }
    default:
      break;
  }
}