// This file's DBG() calls (and dec_vm_loader.h's, included below) fire on
// CONFIG_DBG_GLOBAL or this component's own switch (components/utils/Kconfig)
// - see DBG()'s doc comment in utils.h. Must precede dec_vm_loader.h's
// include: DBG_ENABLE has to be defined before that header's own DBG() call
// sites are preprocessed.
#define DBG_ENABLE CONFIG_DBG_ENABLE_SYS_INTERFACE

#include "sys_interface.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include "dec_sys_contracts.h"
#include "dec_vm_loader.h"
#include "sys_buffers.h"
#include "sys_error.h"
#include "utils.h"

static const char* TAG = "sys_interface";

#include <sdkconfig.h>

R_TASK_DEFINE(s_interface_rx_task_handle, CONFIG_SYS_INTERFACE_RX_TASK_STACK_SIZE);

/**
 * @brief Shared RX wake semaphore owned by sys_interface.
 *
 * Any transport producer may signal it; see sys_interface_get_rx_wake_sem().
 */
R_BINARY_SEM_DEFINE(s_rx_wake_sem);

typedef struct {
  uint8_t decoder_header;
  sys_interface_handler_f decoder;
  const char* name;
} sys_interface_decoder_t;

/**
 * @brief Boot-time decoder registry.
 *
 * Decoders are appended to @c s_decoders[0..s_decoder_count-1] and are never
 * removed, so no free-slot scan or per-entry in-use flag is needed.
 */
static sys_interface_decoder_t s_decoders[CONFIG_SYS_INTERFACE_MAX_CLASSES];
static size_t s_decoder_count = 0;

/**
 * @brief Statically allocated frame-tap storage.
 *
 * The receiver task populates it while capture is enabled and
 * sys_interface_tap_poll() drains it. Direct sys_interface_decode() calls are
 * not tapped. The buffer never allocates or releases heap memory.
 */
R_RINGBUFFER_DEFINE(s_tap_ringbuffer, CONFIG_SYS_ACTIONS_TAP_BUFFER_SIZE, RINGBUF_TYPE_NOSPLIT);
static sys_buff_t s_tap_buff;
static volatile bool s_tap_capture = false;

/**
 * @brief Nesting depth for best-effort RX suspension.
 *
 * This mirrors SE_suspend()/SE_resume() and is a signal to the receiver task,
 * not a hard synchronization barrier.
 */
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

/**
 * @brief Registered RX sources and their static frame storage.
 *
 * Sources are appended to @c s_rx_sources[0..s_rx_source_count-1] and are
 * never removed, so no free-slot scan or unregister bookkeeping is required.
 */
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

  const uint8_t decoder_header = data[0];
  for (size_t i = 0; i < s_decoder_count; i++) {
    if (s_decoders[i].decoder_header == decoder_header) {
      DBG(ESP_LOGI(TAG, "routing frame [%u bytes] to decoder 0x%02X (%s)", (unsigned)len, decoder_header, s_decoders[i].name ? s_decoders[i].name : "unnamed"));
      return s_decoders[i].decoder(data + 1, len - 1);
    }
  }

  DBG(ESP_LOGW(TAG, "no decoder registered for class 0x%02X", decoder_header));
  SE_RET_ERR(ERR_INTERFACE_UNKNOWN_CLASS, .class_header = decoder_header);
}

#undef OWNER
#define OWNER OWNER_SYS_INTERFACE_CLASS
err_h sys_interface_register_decoder(uint8_t decoder_header, sys_interface_handler_f decoder, const char* name) {
  SE_CHECK_NOT_NULL(decoder);

  for (size_t i = 0; i < s_decoder_count; i++) {
    if (s_decoders[i].decoder_header == decoder_header) {
      SE_RET_ERR(ERR_INTERFACE_CLASS_TAKEN, .class_header = decoder_header);
    }
  }
  if (s_decoder_count >= CONFIG_SYS_INTERFACE_MAX_CLASSES) {
    SE_RET_ERR(ERR_INTERFACE_NO_CLASS_SLOTS, .class_header = decoder_header);
  }

  s_decoders[s_decoder_count] = (sys_interface_decoder_t){
      .decoder_header = decoder_header,
      .decoder = decoder,
      .name = name,
  };
  ESP_LOGI(TAG, "registered decoder 0x%02X (%s) in slot %u", decoder_header, name ? name : "unnamed", (unsigned)s_decoder_count);
  s_decoder_count++;
  return NULL;
}

err_h sys_interface_init(void) {
  s_decoder_count = 0;
  s_tap_buff = (sys_buff_t){
      .buff = s_tap_ringbuffer,
      .header = 0,
      .truncated = 0,
  };
  SE_RET_IF_ERR(sys_interface_register_decoder(SYS_CONTRACTS_CLASS_HEADER, dec_sys_contracts_decode, "sys_contracts"));
  SE_RET_IF_ERR(sys_interface_register_decoder(VM_LOADER_CLASS_HEADER, dec_vm_loader_decode, "vm_loader"));
  return NULL;
}
#undef OWNER

#define OWNER OWNER_SYS_INTERFACE_DECODE
void sys_interface_tap_capture_start(void) {
  s_tap_capture = true;
}

void sys_interface_tap_capture_end(void) {
  s_tap_capture = false;
}

err_h sys_interface_tap_poll(uint8_t* buf, size_t max_len, size_t* out_len) {
  SE_CHECK_NOT_NULL(buf);
  SE_CHECK_NOT_NULL(out_len);

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

/**
 * @brief Drain all registered RX sources and dispatch their frames.
 *
 * The shared semaphore wakes this single receiver task immediately. A timeout
 * also wakes it periodically so sources that do not signal are still polled.
 *
 * @param arg Unused FreeRTOS task argument.
 */
static void sys_interface_receiver_task(void* arg) {
  (void)arg;
  ESP_LOGI(TAG, "RX receiver started");

  while (1) {
    BaseType_t got_signal = xSemaphoreTake(s_rx_wake_sem, pdMS_TO_TICKS(CONFIG_SYS_INTERFACE_RX_WAIT_MS));

    if (sys_interface_is_rx_suspended()) {
      /** Preserve a received wake signal while yielding during suspension. */
      if (got_signal) {
        vTaskDelay(pdMS_TO_TICKS(1));
        xSemaphoreGive(s_rx_wake_sem);
      }
      continue;
    }

    for (size_t i = 0; i < s_rx_source_count; i++) {
      sys_interface_rx_source_t* src = &s_rx_sources[i];

      /** Drain this source until its dequeue callback reports empty. */
      while (1) {
        size_t len = 0;
        err_h dq_err = src->dequeue_fn(src->ctx, src->frame, src->max_frame_len, &len);
        if (SE_IS_ERR(dq_err)) {
          SE_ORIGIN_CALL(dq_err);
          break;
        }
        if (len == 0) break;

        if (s_tap_capture) {
          SE_ORIGIN_CALL(sys_buff_push(&s_tap_buff, src->frame, len, 0));

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
      /** Roll back because no task exists to drain the registered source. */
      s_rx_source_count--;
      SE_RET_ERR(ERR_BASE_NO_MEM, 0);
    }
  }

  return NULL;
}

#undef OWNER
