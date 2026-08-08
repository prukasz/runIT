#include "sys_interface.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include "dec_sys_contracts.h"
#include "dec_vm_loader.h"
#include "sys_ble.h"
#include "sys_buffers.h"
#include "sys_error.h"
#include "utils.h"

static const char* TAG = "sys_interface";

#include <sdkconfig.h>

R_TASK_DEFINE(s_interface_rx_task_handle, CONFIG_SYS_INTERFACE_RX_TASK_STACK_SIZE);

// Owned by sys_interface (not any one transport) so any producer can share
// it - see sys_interface_get_rx_wake_sem().
R_BINARY_SEM_DEFINE(s_rx_wake_sem);

typedef struct {
  uint8_t class_header;
  sys_interface_handler_f handler;
  const char* name;
} sys_interface_class_t;

// Classes are only ever appended (0..s_class_count-1), never removed - setup-
// only bookkeeping, so a plain count replaces the old in_use-flag + free-slot scan.
static sys_interface_class_t s_classes[CONFIG_SYS_INTERFACE_MAX_CLASSES];
static size_t s_class_count = 0;

// Tap buffer: populated only by the receiver task (see sys_interface_receiver_task()),
// drained by whoever polls sys_interface_tap_poll(). Never touched from inside
// sys_interface_decode() itself, so a direct decode() call (e.g. sys_actions'
// replay) is never tapped - see SYS_ACTIONS.MD.
static sys_buff_t s_tap_buff;
static bool s_tap_enabled = false;

// Nesting-safe, same shape as sys_error.c's SE_suspend()/SE_resume() - a
// best-effort signal to sys_interface_receiver_task(), not a hard barrier.
static volatile int8_t s_rx_suspend_depth = 0;

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
  sys_interface_rx_dequeue_f dequeue_fn;
  void* ctx;
  uint8_t* frame;
  size_t max_frame_len;
  const char* name;
} sys_interface_rx_source_t;

// Sources are only ever appended (0..s_rx_source_count-1), never removed -
// same plain-count shape as s_classes[] above, for the same reason (setup-time
// bookkeeping, no need for a free-slot scan or unregister).
static sys_interface_rx_source_t s_rx_sources[CONFIG_SYS_INTERFACE_MAX_RX_SOURCES];
static size_t s_rx_source_count = 0;
static uint8_t s_rx_frames[CONFIG_SYS_INTERFACE_MAX_RX_SOURCES][CONFIG_SYS_INTERFACE_RX_FRAME_CAP];

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
  if (s_class_count >= CONFIG_SYS_INTERFACE_MAX_CLASSES) {
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
  /* Registered here rather than self-registered from the VM (the sys_actions
     pattern) because codecs already REQUIRES VM, so a VM -> codecs dependency
     for the decoder header would be circular. Worth moving into a vm_init()
     once the VM owns one, alongside the supervisor start. */
  SE_RET_IF_ERR(sys_interface_register_class(VM_LOADER_CLASS_HEADER, dec_vm_loader_decode, "vm_loader"));
  return NULL;
}
#undef OWNER

#define OWNER OWNER_SYS_INTERFACE_DECODE
err_h sys_interface_tap_enable(size_t buf_size) {
  if (s_tap_enabled) return NULL;
  SE_RET_IF_ERR(sys_buff_init(&s_tap_buff, 0, buf_size));
  s_tap_enabled = true;
  return NULL;
}

void sys_interface_tap_disable(void) {
  if (!s_tap_enabled) return;
  s_tap_enabled = false;
  sys_buff_free(&s_tap_buff);
}

err_h sys_interface_tap_poll(uint8_t* buf, size_t max_len, size_t* out_len) {
  SE_CHECK_NOT_NULL(buf);
  SE_CHECK_NOT_NULL(out_len);
  if (!s_tap_enabled) {
    *out_len = 0;
    return NULL;
  }

  err_h pop_res = sys_buff_pop_raw(&s_tap_buff, buf, max_len, out_len);
  if (SE_IS_ERR(pop_res)) {
    if (pop_res->tag == ERR_BASE_NOT_FOUND) {
      *out_len = 0;
      return NULL;
    }
    return pop_res;
  }
  return NULL;
}
#undef OWNER

#define OWNER OWNER_SYS_INTERFACE_SOURCE

SemaphoreHandle_t sys_interface_get_rx_wake_sem(void) {
  return s_rx_wake_sem;
}

// Single receiver task, all sources processed by this task.
// Task can be woken up by triggering shared semaphore, though pooled every 100ms for non semaphore sources
// When woken up checks all buffs
static void sys_interface_receiver_task(void* arg) {
  (void)arg;
  ESP_LOGI(TAG, "RX receiver started");

  while (1) {
    BaseType_t got_signal = xSemaphoreTake(s_rx_wake_sem, pdMS_TO_TICKS(CONFIG_SYS_INTERFACE_RX_WAIT_MS));

    if (sys_interface_is_rx_suspended()) {
      // give time for other users but still mark that packet not processes
      if (got_signal) {
        vTaskDelay(pdMS_TO_TICKS(1));
        xSemaphoreGive(s_rx_wake_sem);
      }
      continue;
    }

    for (size_t i = 0; i < s_rx_source_count; i++) {
      sys_interface_rx_source_t* src = &s_rx_sources[i];

      // drain evertyhing empty
      while (1) {
        size_t len = 0;
        err_h dq_err = src->dequeue_fn(src->ctx, src->frame, src->max_frame_len, &len);
        if (SE_IS_ERR(dq_err)) {
          SE_ORIGIN_CALL(dq_err);
          break;
        }
        if (len == 0) break;

        ESP_LOGI(TAG, "RX frame [%u bytes] from source %s", (unsigned)len, src->name ? src->name : "unnamed");

        if (s_tap_enabled) {
          err_h tap_err = sys_buff_push(&s_tap_buff, src->frame, len, 0);
          if (SE_IS_ERR(tap_err)) {
            ESP_LOGW(TAG, "tap buffer full - dropping tapped copy of %u byte frame from %s", (unsigned)len, src->name ? src->name : "unnamed");
          }
        }

        SE_ORIGIN_CALL(sys_interface_decode(src->frame, len));
      }
    }
  }
}

err_h sys_interface_register_rx_source(sys_interface_rx_dequeue_f dequeue_fn, void* ctx, size_t max_frame_len, const char* name) {
  SE_CHECK_NOT_NULL(dequeue_fn);
  SE_CHECK_IN_RANGE(max_frame_len, 1, CONFIG_SYS_INTERFACE_RX_FRAME_CAP);
  if (s_rx_source_count >= CONFIG_SYS_INTERFACE_MAX_RX_SOURCES) {
    SE_RET_ERR(ERR_INTERFACE_NO_SOURCE_SLOTS, 0);
  }

  sys_interface_rx_source_t* src = &s_rx_sources[s_rx_source_count];
  src->dequeue_fn = dequeue_fn;
  src->ctx = ctx;
  src->frame = s_rx_frames[s_rx_source_count];
  src->max_frame_len = max_frame_len;
  src->name = name;

  ESP_LOGI(TAG, "registered RX source: %s (slot %u)", name ? name : "unnamed", (unsigned)s_rx_source_count);
  s_rx_source_count++;

  if (s_interface_rx_task_handle == NULL) {
    R_TASK_START(s_interface_rx_task_handle, sys_interface_receiver_task, NULL, CONFIG_SYS_INTERFACE_RX_TASK_PRIO);
    if (s_interface_rx_task_handle == NULL) {
      s_rx_source_count--;  // roll back - nothing will ever drain this slot otherwise
      SE_RET_ERR(ERR_BASE_NO_MEM, 0);
    }
  }

  return NULL;
}

// char_uuid fits in a pointer-sized value, so it's carried as ctx directly
// rather than through an extra per-source allocation.
static err_h sys_ble_rx_dequeue_adapter(void* ctx, uint8_t* buf, size_t max_len, size_t* out_len) {
  uint16_t char_uuid = (uint16_t)(uintptr_t)ctx;
  return sys_ble_char_rx_dequeue(char_uuid, buf, max_len, out_len);
}

err_h sys_interface_bind_ble_rx(uint16_t char_uuid, size_t max_frame_len) {
  // Verify that char can even receive before add
  SE_RET_IF_ERR(sys_ble_char_check_rx_enabled(char_uuid));

  return sys_interface_register_rx_source(sys_ble_rx_dequeue_adapter, (void*)(uintptr_t)char_uuid, max_frame_len, "ble_rx");
}
#undef OWNER
