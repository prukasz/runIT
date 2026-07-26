#include "sys_interface.h"
#include <stdlib.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include "dec_sys_contracts.h"
#include "sys_ble.h"
#include "sys_error.h"

static const char* TAG = "sys_interface";

/** @brief Stack depth of every worker task spawned by sys_interface_bind_ble_rx(). */
#define SYS_INTERFACE_RX_TASK_STACK 4096
/** @brief Priority of every worker task spawned by sys_interface_bind_ble_rx(). */
#define SYS_INTERFACE_RX_TASK_PRIO 5
/** @brief Semaphore wait slice; also the fallback poll period if no semaphore exists. */
#define SYS_INTERFACE_RX_WAIT_MS 100

typedef struct {
  uint8_t class_header;
  bool in_use;
  sys_interface_handler_f handler;
  const char* name;
} sys_interface_class_t;

static sys_interface_class_t s_classes[SYS_INTERFACE_MAX_CLASSES];

typedef struct {
  uint16_t char_uuid;
  size_t max_frame_len;
  uint8_t* frame;
  SemaphoreHandle_t rx_sem;
} sys_interface_ble_src_t;

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

  const uint8_t class_header = data[0];
  for (size_t i = 0; i < SYS_INTERFACE_MAX_CLASSES; i++) {
    if (s_classes[i].in_use && s_classes[i].class_header == class_header) {
      ESP_LOGI(TAG, "routing frame [%u bytes] to class 0x%02X (%s)", (unsigned)len, class_header,
               s_classes[i].name ? s_classes[i].name : "unnamed");
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

  int free_slot = -1;
  for (size_t i = 0; i < SYS_INTERFACE_MAX_CLASSES; i++) {
    if (s_classes[i].in_use && s_classes[i].class_header == class_header) {
      SE_RET_ERR(ERR_INTERFACE_CLASS_TAKEN, .class_header = class_header);
    }
    if (!s_classes[i].in_use && free_slot < 0) free_slot = (int)i;
  }
  if (free_slot < 0) {
    SE_RET_ERR(ERR_INTERFACE_NO_CLASS_SLOTS, .class_header = class_header);
  }

  s_classes[free_slot] = (sys_interface_class_t){
      .class_header = class_header,
      .in_use = true,
      .handler = handler,
      .name = name,
  };
  ESP_LOGI(TAG, "registered class 0x%02X (%s) in slot %d", class_header, name ? name : "unnamed", free_slot);
  return NULL;
}

err_h sys_interface_unregister_class(uint8_t class_header) {
  for (size_t i = 0; i < SYS_INTERFACE_MAX_CLASSES; i++) {
    if (s_classes[i].in_use && s_classes[i].class_header == class_header) {
      ESP_LOGI(TAG, "unregistered class 0x%02X (%s)", class_header, s_classes[i].name ? s_classes[i].name : "unnamed");
      s_classes[i] = (sys_interface_class_t){0};
      return NULL;
    }
  }
  SE_RET_ERR(ERR_INTERFACE_UNKNOWN_CLASS, .class_header = class_header);
}

err_h sys_interface_init(void) {
  memset(s_classes, 0, sizeof(s_classes));
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
      ESP_LOGI(TAG, "RX frame [%u bytes] from characteristic 0x%04X", (unsigned)len, src->char_uuid);
      SE_ORIGIN_CALL(sys_interface_decode(src->frame, len));
    }
  }
}

err_h sys_interface_bind_ble_rx(uint16_t char_uuid, size_t max_frame_len) {
  if (max_frame_len == 0) {
    SE_RET_ERR(ERR_INVALID_VAL_UI32, .val = 0, .min = 1, .max = UINT32_MAX);
  }

  // Non-destructive probe: errors if the characteristic doesn't exist, and the
  // handle is NULL exactly when RX is disabled (rx_buffer_size == 0).
  SemaphoreHandle_t rx_sem = NULL;
  SE_RET_IF_ERR(sys_ble_char_get_rx_semaphore(char_uuid, &rx_sem));
  if (rx_sem == NULL) {
    ESP_LOGE(TAG, "characteristic 0x%04X has no RX buffer - nothing to bind", char_uuid);
    SE_RET_ERR(ERR_BASE_INVALID_STATE, 0);
  }

  sys_interface_ble_src_t* src = calloc(1, sizeof(sys_interface_ble_src_t));
  SE_CHECK_IF_ALLOCATED(src);

  src->char_uuid = char_uuid;
  src->max_frame_len = max_frame_len;
  src->rx_sem = rx_sem;
  src->frame = malloc(max_frame_len);
  if (src->frame == NULL) {
    free(src);
    SE_RET_ERR(ERR_BASE_NO_MEM, 0);
  }

  if (xTaskCreate(sys_interface_ble_rx_task, "sys_if_rx", SYS_INTERFACE_RX_TASK_STACK, src, SYS_INTERFACE_RX_TASK_PRIO, NULL) != pdPASS) {
    free(src->frame);
    free(src);
    SE_RET_ERR(ERR_BASE_NO_MEM, 0);
  }

  return NULL;
}
#undef OWNER
