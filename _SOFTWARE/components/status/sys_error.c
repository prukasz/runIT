#include "sys_error.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <string.h>

static const char* TAG = "sys_error";

#define ERR_BUF_SIZE 1024
static uint8_t err_buffer[ERR_BUF_SIZE] __attribute__((aligned(8)));
static uint32_t head_idx = 0;
static volatile int8_t s_suspend_depth = 0;

// Each payload must fit comfortably within the ring so a single allocation
// can't approach wiping out the whole buffer's worth of in-flight chains.
#define X_CHK(tag, struct_def)                                                       \
  _Static_assert(sizeof(sys_err_t) + sizeof(err_payload_##tag##_t) <= ERR_BUF_SIZE / 4, \
                 #tag " payload too large for the error ring");
SYS_ERROR_MAP(X_CHK)
#undef X_CHK

#define ERR_QUEUE_LEN 32
static QueueHandle_t s_err_queue = NULL;
static StaticQueue_t s_err_queue_struct;
static uint8_t s_err_queue_storage[ERR_QUEUE_LEN * sizeof(err_h)];

const char* SE_get_owner_name(uint32_t owner) {
  switch (owner) {
#define X_OWNER_CASE(tag, id, name) \
  case id:                          \
    return name;
    SYS_OWNER_MAP(X_OWNER_CASE)
#undef X_OWNER_CASE
    default:
      return "OWNER_UNKNOWN";
  }
}

const char* SE_get_tag_name(err_tag_e tag) {
  switch (tag) {
#define X_TAG_CASE(tag_name, struct_def) \
  case tag_name:                         \
    return #tag_name;
    SYS_ERROR_MAP(X_TAG_CASE)
#undef X_TAG_CASE
    default:
      return "TAG_UNKNOWN";
  }
}

static void sys_error_handler_task(void* arg) {
  (void)arg;
  err_h err_chain = NULL;
  while (1) {
    if (xQueueReceive(s_err_queue, &err_chain, portMAX_DELAY) == pdTRUE) {
      if (!err_chain) continue;
      ESP_LOGE(TAG, "========== ERROR STACK TRACE ==========");
      int depth = 0;
      for (err_h curr = err_chain; curr != NULL; curr = curr->next_cause) {
        ESP_LOGE(TAG, "  [%d] Owner: %s (0x%04X), Tag: %s (%d)", depth++, SE_get_owner_name(curr->owner), (unsigned int)curr->owner, SE_get_tag_name(curr->tag), (int)curr->tag);
      }
      ESP_LOGE(TAG, "=======================================");
    }
  }
}

void SE_init(void) {
  if (s_err_queue == NULL) {
    s_err_queue = xQueueCreateStatic(ERR_QUEUE_LEN, sizeof(err_h), s_err_queue_storage, &s_err_queue_struct);
  }
  static bool s_task_created = false;
  if (!s_task_created) {
    xTaskCreate(sys_error_handler_task, "sys_err_hdlr", 4096, NULL, 5, NULL);
    s_task_created = true;
  }
}

err_h SE_alloc_bytes(size_t payload_size, err_tag_e tag, uint32_t owner) {
  uint32_t total_size = sizeof(sys_err_t) + payload_size;
  total_size = (total_size + 7) & ~7u;  // align to 8 bytes

  uint32_t old_head, new_head, alloc_idx;
  do {
    old_head = __atomic_load_n(&head_idx, __ATOMIC_RELAXED);

    if (old_head + total_size > ERR_BUF_SIZE) {
      alloc_idx = 0;
      new_head = total_size;
    } else {
      alloc_idx = old_head;
      new_head = old_head + total_size;
    }
  } while (!__atomic_compare_exchange_n(&head_idx, &old_head, new_head, false, __ATOMIC_SEQ_CST, __ATOMIC_RELAXED));

  err_h err = (err_h)&err_buffer[alloc_idx];
  memset(err, 0, sizeof(sys_err_t));  // payload is zero-filled by the compound-literal assignment
  err->tag = tag;
  err->owner = owner;
  err->next_cause = NULL;

  return err;
}

void SE_push_to_handler(err_h err) {
  if (!err || SE_is_suspended() || !s_err_queue) return;

  if (xPortInIsrContext()) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(s_err_queue, &err, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) {
      portYIELD_FROM_ISR();
    }
  } else {
    xQueueSend(s_err_queue, &err, 0);
  }
}

// Nesting-safe: a suspended section may call into another function that
// also suspends/resumes without prematurely re-enabling error reporting.
void SE_suspend(void) {
  s_suspend_depth++;
}
void SE_resume(void) {
  if (s_suspend_depth > 0) {
    s_suspend_depth--;
  }
}
bool SE_is_suspended(void) {
  return s_suspend_depth > 0;
}
