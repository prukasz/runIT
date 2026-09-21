// This file's DBG() calls (and dec_vm_loader.h's, included below) fire on
// CONFIG_DBG_GLOBAL or this component's own switch (components/utils/Kconfig)
// - see DBG()'s doc comment in utils.h. Must precede dec_vm_loader.h's
// include: DBG_ENABLE has to be defined before that header's own DBG() call
// sites are preprocessed.
#define DBG_ENABLE CONFIG_DBG_ENABLE_SYS_INTERFACE

#include "sys_interface.h"
#include "dec_sys_contracts.h"
#include "dec_vm_loader.h"
#include "dec_features.h"
#include "sys_buffers.h"
#include "utils.h"
#include <sdkconfig.h>

static const char* TAG = "sys_interface";


R_TASK_DEFINE(s_interface_rx_task_handle, CONFIG_SYS_INTERFACE_RX_TASK_STACK_SIZE);
static void sys_interface_receiver_task(void* arg);

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
      .truncated = 0,
  };
  SE_RET_IF_ERR(sys_interface_register_decoder(CONFIG_RX_PACKET_CLASS_SYS_CONTRACTS, dec_sys_contracts_decode, "sys_contracts"));
  SE_RET_IF_ERR(sys_interface_register_decoder(CONFIG_RX_PACKET_CLASS_VM_LOADER, dec_vm_loader_decode, "vm_loader"));
  SE_RET_IF_ERR(sys_interface_register_decoder(CONFIG_RX_PACKET_CLASS_SYS_FEATURES, dec_features_decode, "features"));

  sys_data_connector_t* conn = sys_data_connector_get(SYS_INTERFACE_CONNECTOR_ID);
  if (!conn) {
    SE_RET_ERR(ERR_BASE_NO_MEM, 0);
  }

  if (s_interface_rx_task_handle == NULL) {
    R_TASK_START(s_interface_rx_task_handle, sys_interface_receiver_task, NULL, CONFIG_SYS_INTERFACE_RX_TASK_PRIO);
    if (s_interface_rx_task_handle == NULL) {
      SE_RET_ERR(ERR_BASE_NO_MEM, 0);
    }
  }

  return NULL;
}
#undef OWNER

#define OWNER OWNER_SYS_INTERFACE_BASE
err_h sys_interface_send(const void* data, size_t len) {
  SE_CHECK_NOT_NULL(data);
  if (len == 0) return NULL;
  sys_data_connector_t* conn = sys_data_connector_get(SYS_INTERFACE_CONNECTOR_ID);
  if (!conn) {
    SE_RET_ERR(ERR_BASE_NOT_FOUND, 0);
  }
  sys_data_connector_send(conn, data, len);
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

  err_h pop_res = sys_buff_pop(&s_tap_buff, buf, max_len, out_len);
  if (SE_IS_ERR(pop_res)) {
    if (pop_res->tag == ERR_BASE_NOT_FOUND) {
      *out_len = 0;
      SE_release(pop_res);
      return NULL;
    }
    return pop_res;
  }
  return NULL;
}
#undef OWNER

#define OWNER OWNER_SYS_INTERFACE_SOURCE

static uint8_t s_rx_frame[CONFIG_SYS_INTERFACE_RX_FRAME_CAP];

/**
 * @brief Drain incoming frames from the interface data connector and dispatch them.
 *
 * Waits on the connector's data_present semaphore (with CONFIG_SYS_INTERFACE_RX_WAIT_MS timeout)
 * and pulls frames via sys_data_connector_receive().
 *
 * @param arg Unused FreeRTOS task argument.
 */
static void sys_interface_receiver_task(void* arg) {
  (void)arg;
  ESP_LOGI(TAG, "RX receiver started");

  sys_data_connector_t* conn = sys_data_connector_get(SYS_INTERFACE_CONNECTOR_ID);
  if (!conn || !conn->data_present) {
    ESP_LOGE(TAG, "Failed to obtain interface connector for RX receiver");
    vTaskDelete(NULL);
    return;
  }

  while (1) {
    BaseType_t got_signal = xSemaphoreTake(conn->data_present, pdMS_TO_TICKS(CONFIG_SYS_INTERFACE_RX_WAIT_MS));

    if (sys_interface_is_rx_suspended()) {
      /** Preserve a received wake signal while yielding during suspension. */
      if (got_signal) {
        vTaskDelay(pdMS_TO_TICKS(1));
        xSemaphoreGive(conn->data_present);
      }
      continue;
    }

    while (1) {
      size_t len = 0;
      err_h dq_err = sys_data_connector_receive(conn, s_rx_frame, sizeof(s_rx_frame), &len);
      if (SE_IS_ERR(dq_err)) {
        SE_ORIGIN_CALL(dq_err);
        break;
      }
      if (len == 0) break;

      if (s_tap_capture) {
        SE_ORIGIN_CALL(sys_buff_push(&s_tap_buff, s_rx_frame, len, 0));
      }
      SE_ORIGIN_CALL(sys_interface_decode(s_rx_frame, len));
    }
  }
}

#undef OWNER

__attribute__((weak)) err_h sys_interface_handle_fault(err_h node, err_h chain) {
  (void)chain;
  if (!node || SE_get_tag_level(node->tag) != SYS_DEV_ERR_CRITICAL) {
    return NULL;
  }
  // Severe interface fault: suspend RX to prevent corrupt frame flooding
  sys_interface_suspend_rx();
  return NULL;
}

