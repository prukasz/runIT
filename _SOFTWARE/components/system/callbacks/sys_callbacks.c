#include "sys_callbacks.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <string.h>
#include "utils.h"

#define TAG "SYS_CB"
#define OWNER OWNER_SYS_DEVICE_BASE

#define CALLBACK_QUEUE_LEN 16
R_QUEUE_DEFINE(s_callback_queue, CALLBACK_QUEUE_LEN, sizeof(cb_event_t));

R_TASK_DEFINE(s_callback_task_handle, 4096);

typedef void (*sys_cb_route_func_t)(const cb_event_t* event);

static void sys_cb_route_logger(const cb_event_t* event) {
  switch (event->head.callback_type) {
    case CALLBACK_IO:
      ESP_LOGI(TAG, "Received IO Event: Device ID %u, Pin %u, Event %u, Val %ld", event->event.io.device_id, event->event.io.pin_id, event->event.io.trigger_event, (long)event->event.io.trigger_value);
      break;
    case CALLBACK_PWR:
      ESP_LOGI(TAG, "Received PWR Event: Device ID %u, Channel %u, Event %u, Val %ld", event->event.pwr.device_id, event->event.pwr.channel_id, event->event.pwr.trigger_event, (long)event->event.pwr.trigger_value);
      break;
    case CALLBACK_BLE:
      ESP_LOGI(TAG, "Received BLE Event: Event %lu, Val %ld", (unsigned long)event->event.ble.event, (long)event->event.ble.value);
      break;
    default:
      break;
  }
}

static void sys_cb_route_ble(const cb_event_t* event) {
  (void)event;
  // TODO: BLE-specific routing logic. Stub for now - see SYS_CALLBACKS.MD.
}

// Bit i of an event's route_mask selects s_route_table[i]. Slots left NULL
// (e.g. SYS_CB_ROUTE_WIFI) are silently skipped by sys_cb_task, not an error -
// this is how new routes get added progressively without touching the task.
static const sys_cb_route_func_t s_route_table[SYS_CB_ROUTE_COUNT] = {
    [SYS_CB_ROUTE_LOGGER] = sys_cb_route_logger,
    [SYS_CB_ROUTE_BLE] = sys_cb_route_ble,
};

static void sys_cb_task(void* pvParameters) {
  (void)pvParameters;
  cb_event_t event;

  while (1) {
    if (!R_QUEUE_RECEIVE(s_callback_queue, &event, WAIT_FOREVER)) continue;

    if (event.head.callback_type == CALLBACK_OWN_FUNC) {
      if (event.event.own_func.own_func) {
        event.event.own_func.own_func(event.event.own_func.device_handle, &event);
      }
      continue;
    }

    for (int i = 0; i < SYS_CB_ROUTE_COUNT; i++) {
      if ((event.head.route_mask & SYS_CB_ROUTE_BIT(i)) && s_route_table[i]) {
        s_route_table[i](&event);
      }
    }
  }
}

err_h sys_callbacks_init(void) {
  if (s_callback_task_handle == NULL) {
    R_TASK_START_ON_CORE(s_callback_task_handle, sys_cb_task, NULL, 5, 0);
  }

  ESP_LOGI(TAG, "Callback system initialized successfully");
  return NULL;
}

err_h sys_callback_trigger(const cb_event_t* event) {
  if (!event) {
    SE_RET_ERR(ERR_INVALID_VAL_UI32, 0, 1, UINT32_MAX);
  }
  BaseType_t in_isr = xPortInIsrContext();
  if (in_isr) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (xQueueSendFromISR(s_callback_queue, event, &xHigherPriorityTaskWoken) != pdTRUE) {
      SE_RET_ERR(ERR_BASE_NO_MEM, 0);
    }
    if (xHigherPriorityTaskWoken) {
      portYIELD_FROM_ISR();
    }
  } else {
    if (R_QUEUE_SEND(s_callback_queue, event, WAIT_FOREVER) != pdTRUE) {
      SE_RET_ERR(ERR_BASE_NO_MEM, 0);
    }
  }

  return NULL;
}
