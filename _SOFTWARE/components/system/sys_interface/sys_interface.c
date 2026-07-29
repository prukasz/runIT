#include "sys_interface.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include "dec_sys_contracts.h"
#include "sys_ble.h"
#include "sys_error.h"
#include "utils.h"


static const char* TAG = "sys_interface";

/** @brief Stack depth of the worker task spawned by sys_interface_bind_ble_rx(). */
#define SYS_INTERFACE_RX_TASK_STACK 4096
/** @brief Priority of the worker task spawned by sys_interface_bind_ble_rx(). */
#define SYS_INTERFACE_RX_TASK_PRIO 5
/** @brief Semaphore wait slice; also the fallback poll period if no semaphore exists. */
#define SYS_INTERFACE_RX_WAIT_MS 100
/** @brief Largest frame sys_interface_bind_ble_rx() can be bound with - sized to the one caller (RUNIT_BLE_RX_FRAME_MAX). */
#define SYS_INTERFACE_RX_FRAME_CAP 512

R_TASK_DEFINE(s_interface_rx_task_handle, SYS_INTERFACE_RX_TASK_STACK);

typedef struct {
  uint8_t class_header;
  sys_interface_handler_f handler;
  const char* name;
} sys_interface_class_t;

// Classes are only ever appended (0..s_class_count-1), never removed - setup-
// only bookkeeping, so a plain count replaces the old in_use-flag + free-slot scan.
static sys_interface_class_t s_classes[SYS_INTERFACE_MAX_CLASSES];
static size_t s_class_count = 0;

static sys_interface_tap_f s_tap = NULL;

// Nesting-safe, same shape as sys_error.c's SE_suspend()/SE_resume() - a
// best-effort signal to sys_interface_ble_rx_task(), not a hard barrier.
static volatile int8_t s_rx_suspend_depth = 0;

void sys_interface_set_tap(sys_interface_tap_f tap) {
  s_tap = tap;
}

void sys_interface_suspend_rx(void) {
  s_rx_suspend_depth++;
}

void sys_interface_resume_rx(void) {
  if (s_rx_suspend_depth > 0) s_rx_suspend_depth--;
}

bool sys_interface_is_rx_suspended(void) {
  return s_rx_suspend_depth > 0;
}

typedef struct {
  uint16_t char_uuid;
  size_t max_frame_len;
  uint8_t* frame;
  SemaphoreHandle_t rx_sem;
} sys_interface_ble_src_t;

static sys_interface_ble_src_t s_ble_src;
static uint8_t s_ble_rx_frame[SYS_INTERFACE_RX_FRAME_CAP];

#undef OWNER
#define OWNER OWNER_SYS_INTERFACE_DECODE

err_h convert_to_packet(const uint8_t* data, size_t len, void* packet, size_t packet_size) {
  if (len < packet_size) {
    SE_RET_ERR(ERR_INTERFACE_SHORT_FRAME, .got = (uint32_t)len, .need = (uint32_t)packet_size);
  }
  memcpy(packet, data, packet_size);
  return NULL;
}

err_h sys_interface_decode(const uint8_t* data, size_t len) {
  SE_CHECK_NOT_NULL(data);
  if (len == 0) {
    SE_RET_ERR(ERR_INTERFACE_SHORT_FRAME, .got = 0, .need = 1);
  }

  if (s_tap) s_tap(data, len);

  const uint8_t class_header = data[0];
  for (size_t i = 0; i < s_class_count; i++) {
    if (s_classes[i].class_header == class_header) {
      ESP_LOGI(TAG, "routing frame [%u bytes] to class 0x%02X (%s)", (unsigned)len, class_header, s_classes[i].name ? s_classes[i].name : "unnamed");
      return s_classes[i].handler(data + 1, len - 1);
    }
  }

  ESP_LOGW(TAG, "no handler registered for class 0x%02X", class_header);
  SE_RET_ERR(ERR_INTERFACE_UNKNOWN_CLASS, .class_header = class_header);
}

#undef OWNER
#define OWNER OWNER_SYS_INTERFACE_CLASS
err_h sys_interface_register_class(uint8_t class_header, sys_interface_handler_f handler, const char* name) {
  SE_CHECK_NOT_NULL(handler);

  for (size_t i = 0; i < s_class_count; i++) {
    if (s_classes[i].class_header == class_header) {
      SE_RET_ERR(ERR_INTERFACE_CLASS_TAKEN, .class_header = class_header);
    }
  }
  if (s_class_count >= SYS_INTERFACE_MAX_CLASSES) {
    SE_RET_ERR(ERR_INTERFACE_NO_CLASS_SLOTS, .class_header = class_header);
  }

  s_classes[s_class_count] = (sys_interface_class_t){
      .class_header = class_header,
      .handler = handler,
      .name = name,
  };
  ESP_LOGI(TAG, "registered class 0x%02X (%s) in slot %u", class_header, name ? name : "unnamed", (unsigned)s_class_count);
  s_class_count++;
  return NULL;
}

err_h sys_interface_init(void) {
  s_class_count = 0;
  SE_RET_IF_ERR(sys_interface_register_class(SYS_CONTRACTS_CLASS_HEADER, dec_sys_contracts_decode, "sys_contracts"));
  return NULL;
}
#undef OWNER

#define OWNER OWNER_SYS_INTERFACE_SOURCE

static void sys_interface_ble_rx_task(void* arg) {
  sys_interface_ble_src_t* src = (sys_interface_ble_src_t*)arg;
  ESP_LOGI(TAG, "RX pump started on characteristic 0x%04X (%s)", src->char_uuid, src->rx_sem ? "event-driven" : "polled");

  while (1) {
    if (src->rx_sem != NULL) {
      xSemaphoreTake(src->rx_sem, pdMS_TO_TICKS(SYS_INTERFACE_RX_WAIT_MS));
    } else {
      vTaskDelay(pdMS_TO_TICKS(SYS_INTERFACE_RX_WAIT_MS));
    }

    // One semaphore give may cover several queued writes - drain until empty.
    while (1) {
      size_t len = 0;
      if (SE_IS_ERR(sys_ble_char_rx_dequeue(src->char_uuid, src->frame, src->max_frame_len, &len))) break;
      if (len == 0) break;

      if (sys_interface_is_rx_suspended()) {
        ESP_LOGW(TAG, "RX suspended - dropping %u byte frame from characteristic 0x%04X", (unsigned)len, src->char_uuid);
        continue;
      }

      ESP_LOGI(TAG, "RX frame [%u bytes] from characteristic 0x%04X", (unsigned)len, src->char_uuid);
      SE_ORIGIN_CALL(sys_interface_decode(src->frame, len));
    }
  }
}

err_h sys_interface_bind_ble_rx(uint16_t char_uuid, size_t max_frame_len) {
  if (max_frame_len == 0 || max_frame_len > SYS_INTERFACE_RX_FRAME_CAP) {
    SE_RET_ERR(ERR_INVALID_VAL_UI32, .val = (uint32_t)max_frame_len, .min = 1, .max = SYS_INTERFACE_RX_FRAME_CAP);
  }
  if (s_interface_rx_task_handle != NULL) {
    ESP_LOGE(TAG, "sys_interface_bind_ble_rx() already bound - only one BLE RX source is supported");
    SE_RET_ERR(ERR_BASE_INVALID_STATE, 0);
  }

  // Non-destructive probe: errors if the characteristic doesn't exist, and the
  // handle is NULL exactly when RX is disabled (rx_buffer_size == 0).
  SemaphoreHandle_t rx_sem = NULL;
  SE_RET_IF_ERR(sys_ble_char_get_rx_semaphore(char_uuid, &rx_sem));
  if (rx_sem == NULL) {
    ESP_LOGE(TAG, "characteristic 0x%04X has no RX buffer - nothing to bind", char_uuid);
    SE_RET_ERR(ERR_BASE_INVALID_STATE, 0);
  }

  s_ble_src.char_uuid = char_uuid;
  s_ble_src.max_frame_len = max_frame_len;
  s_ble_src.rx_sem = rx_sem;
  s_ble_src.frame = s_ble_rx_frame;

  R_TASK_START(s_interface_rx_task_handle, sys_interface_ble_rx_task, &s_ble_src, SYS_INTERFACE_RX_TASK_PRIO);
  if (s_interface_rx_task_handle == NULL) {
    SE_RET_ERR(ERR_BASE_NO_MEM, 0);
  }

  return NULL;
}
#undef OWNER
