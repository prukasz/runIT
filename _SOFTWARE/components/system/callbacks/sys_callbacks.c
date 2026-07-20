#include "sys_callbacks.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <string.h>
#include "utils.h"

#define TAG "SYS_CB"
#define OWNER OWNER_SYS_DEVICE_BASE

static QueueHandle_t s_callback_queue = NULL;
static StaticQueue_t s_callback_queue_buffer;
#define CALLBACK_QUEUE_LEN 16
static uint8_t s_callback_queue_storage[CALLBACK_QUEUE_LEN * sizeof(cb_event_t)];

R_TASK_DEFINE(s_callback_task_handle, 4096);

static void sys_cb_task(void* pvParameters) {
  (void)pvParameters;
  cb_event_t event;

  while (1) {
    if (R_QUEUE_RECEIVE(s_callback_queue, &event, WAIT_FOREVER)) {
      // Dummy / skeleton handler logic
      if (event.head.callback_type == CALLBACK_IO) {
        ESP_LOGI(TAG, "Received IO Event: Device ID %u, Pin %u, Event %u, Val %ld", event.event.io.device_id, event.event.io.pin_id, event.event.io.trigger_event, (long)event.event.io.trigger_value);
      } else if (event.head.callback_type == CALLBACK_PWR) {
        ESP_LOGI(TAG, "Received PWR Event: Device ID %u, Channel %u, Event %u, Val %ld", event.event.pwr.device_id, event.event.pwr.channel_id, event.event.pwr.trigger_event, (long)event.event.pwr.trigger_value);
      } else if (event.head.callback_type == CALLBACK_OWN_FUNC) {
        if (event.event.own_func.own_func) {
          event.event.own_func.own_func(event.event.own_func.device_handle, &event);
        }
      }
    }
  }
}

status_rep_t sys_callbacks_init(void) {
  if (s_callback_queue != NULL) {
    return STA_OK;
  }

  s_callback_queue = xQueueCreateStatic(CALLBACK_QUEUE_LEN, sizeof(cb_event_t), s_callback_queue_storage, &s_callback_queue_buffer);
  if (!s_callback_queue) {
    return STA_C(ERR_NO_MEM, OWNER, 0, STATUS_PAYLOAD_UNKNOWN);
  }

  R_TASK_START_ON_CORE(s_callback_task_handle, sys_cb_task, NULL, 5, 0);

  ESP_LOGI(TAG, "Callback system initialized successfully");
  return STA_OK;
}

status_rep_t sys_callback_trigger(cb_event_t* event) {
  if (!event) {
    return STA_C(ERR_INVALID_ARG, OWNER, 0, STATUS_PAYLOAD_UNKNOWN);
  }

  if (!s_callback_queue) {
    return STA_C(ERR_INVALID_STATE, OWNER, 0, STATUS_PAYLOAD_UNKNOWN);
  }

  BaseType_t in_isr = xPortInIsrContext();
  if (in_isr) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (xQueueSendFromISR(s_callback_queue, event, &xHigherPriorityTaskWoken) != pdTRUE) {
      return STA_C(ERR_NO_MEM, OWNER, 0, STATUS_PAYLOAD_UNKNOWN);
    }
    if (xHigherPriorityTaskWoken) {
      portYIELD_FROM_ISR();
    }
  } else {
    if (xQueueSend(s_callback_queue, event, portMAX_DELAY) != pdTRUE) {
      return STA_C(ERR_NO_MEM, OWNER, 0, STATUS_PAYLOAD_UNKNOWN);
    }
  }

  return STA_OK;
}
