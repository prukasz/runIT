#include "sys_callbacks.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <string.h>
#include "utils.h"

#define TAG "SYS_CB"
#define OWNER OWNER_SYS_DEVICE_BASE

#include <sdkconfig.h>

R_QUEUE_DEFINE(s_callback_queue, CONFIG_SYS_CB_QUEUE_LEN, sizeof(cb_event_t));

R_TASK_DEFINE(s_callback_task_handle, CONFIG_SYS_CB_TASK_STACK_SIZE);

// Bit i of an event's route_mask selects s_route_table[i]. Slots nobody
// registers into (e.g. SYS_CB_ROUTE_WIFI) are silently skipped by sys_cb_task,
// not an error. Filled at runtime by sys_cb_register_route() - each owning
// component (sys_io, sys_power, ble) plugs its own handler in from a
// load-time constructor rather than sys_callbacks knowing about them at
// compile time (see sys_callbacks.h).
static sys_cb_route_func_t s_route_table[CONFIG_SYS_CB_ROUTE_COUNT];

_Static_assert(CONFIG_SYS_CB_ROUTE_COUNT <= 16, "Callback route mask supports at most 16 routes");
static sys_cb_action_executor_f s_action_executor;

void sys_cb_register_action_executor(sys_cb_action_executor_f executor) {
  s_action_executor = executor;
}

static void sys_cb_task(void* pvParameters) {
  (void)pvParameters;
  cb_event_t event;

  while (1) {
    if (!R_QUEUE_RECEIVE(s_callback_queue, &event, WAIT_FOREVER)) continue;

    if (event.head.callback_type == CALLBACK_OWN_FUNC) {
      if (event.event.own_func.own_func) {
        SE_ORIGIN_CALL(event.event.own_func.own_func(event.event.own_func.device_handle, &event));
      }
      continue;
    }

    for (int i = 0; i < CONFIG_SYS_CB_ROUTE_COUNT; i++) {
      if ((event.head.route_mask & SYS_CB_ROUTE_BIT(i)) && s_route_table[i]) {
        s_route_table[i](&event);
      }
    }
    if (event.head.dynamic_action_id || event.head.static_action_id) {
      if (!s_action_executor) {
        SE_ORIGIN_CALL(SE_ERR_NEW(ERR_BASE_INVALID_STATE, 0));
        continue;
      }
      /** User action first, then system action, even if the user action fails. */
      if (event.head.dynamic_action_id) {
        SE_ORIGIN_CALL(s_action_executor(0x01, event.head.dynamic_action_id));
      }
      if (event.head.static_action_id) {
        SE_ORIGIN_CALL(s_action_executor(0x00, event.head.static_action_id));
      }
    }
  }
}

err_h sys_cb_register_route(uint8_t route_idx, sys_cb_route_func_t fn) {
  SE_CHECK_IN_RANGE(route_idx, 0, CONFIG_SYS_CB_ROUTE_COUNT - 1);
  s_route_table[route_idx] = fn;
  return NULL;
}

err_h sys_callbacks_init(void) {
  if (s_callback_task_handle == NULL) {
    R_TASK_START_ON_CORE(s_callback_task_handle, sys_cb_task, NULL, CONFIG_SYS_CB_TASK_PRIO, 0);
    SE_CHECK_IF_ALLOCATED(s_callback_task_handle);
  }

  ESP_LOGI(TAG, "Callback system initialized successfully");
  return NULL;
}

err_h sys_callback_trigger(const cb_event_t* event) {
  SE_CHECK_NOT_NULL(event);
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
    if (R_QUEUE_SEND(s_callback_queue, event, NO_WAIT) != pdTRUE) {
      SE_RET_ERR(ERR_BASE_NO_MEM, 0);
    }
  }

  return NULL;
}
