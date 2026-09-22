// This file's DBG() calls fire on CONFIG_DBG_GLOBAL or this component's own
// switch (components/utils/Kconfig) - see DBG()'s doc comment in utils.h.
#define DBG_ENABLE CONFIG_DBG_ENABLE_SYS_INTERFACE

#include "sys_interface.h"
#include "sys_buffers.h"
#include <string.h>
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
    SE_FAIL(ERR_INTERFACE_SHORT_FRAME, .got = (uint32_t)len, .need = (uint32_t)packet_size);
  }
  memcpy(packet, data, packet_size);
  return NULL;
}

/* Response being built for the live frame the RX task is decoding.
   s_response is task-local and set only by the RX task around one live frame,
   so frames decoded elsewhere (sys_actions replay, other tasks) and frames
   replayed from inside a live one never write into it. */
typedef struct {
  uint8_t buf[3 + CONFIG_SYS_INTERFACE_RESPONSE_MAX]; /* class, packet, status, data */
  size_t len;
} interface_response_t;

static interface_response_t s_live_response; /* RX task only */
static __thread interface_response_t* s_response;

static SE_MUST_USE err_h decode_frame(const uint8_t* data, size_t len) {
  SE_CHECK_NOT_NULL(data);
  if (len == 0) {
    SE_FAIL(ERR_INTERFACE_SHORT_FRAME, .got = 0, .need = 1);
  }

  const uint8_t decoder_header = data[0];
  for (size_t i = 0; i < s_decoder_count; i++) {
    if (s_decoders[i].decoder_header == decoder_header) {
      DBG(ESP_LOGI(TAG, "routing frame [%u bytes] to decoder 0x%02X (%s)", (unsigned)len, decoder_header, s_decoders[i].name ? s_decoders[i].name : "unnamed"));
      return s_decoders[i].decoder(data + 1, len - 1);
    }
  }

  DBG(ESP_LOGW(TAG, "no decoder registered for class 0x%02X", decoder_header));
  SE_FAIL(ERR_INTERFACE_UNKNOWN_CLASS, .class_header = decoder_header);
}

err_h sys_interface_decode(const uint8_t* data, size_t len) {
  interface_response_t* outer = s_response;
  s_response = NULL;
  err_h err = decode_frame(data, len);
  s_response = outer;
  return err;
}

#undef OWNER
#define OWNER OWNER_SYS_INTERFACE_RESPOND
err_h sys_interface_respond(const void* data, size_t len) {
  SE_CHECK_NOT_NULL(data);
  if (s_response == NULL) return NULL;
  size_t data_len = s_response->len - 3;
  if (len > CONFIG_SYS_INTERFACE_RESPONSE_MAX - data_len) {
    SE_FAIL(ERR_INTERFACE_RESPONSE_TOO_LONG, .got = (uint32_t)(data_len + len), .max = CONFIG_SYS_INTERFACE_RESPONSE_MAX);
  }
  memcpy(&s_response->buf[s_response->len], data, len);
  s_response->len += len;
  return NULL;
}

/* Decode one live frame and answer it: [class][packet][status][data]. The
   error chain (if any) is still handed to the error handler afterwards. */
static void decode_live_frame(const uint8_t* data, size_t len) {
  interface_response_t* rsp = &s_live_response;
  rsp->buf[0] = data[0];
  rsp->buf[1] = (len > 1) ? data[1] : 0x00;
  rsp->buf[2] = SYS_INTERFACE_STATUS_OK;
  rsp->len = 3;

  s_response = rsp;
  err_h err = decode_frame(data, len);
  s_response = NULL;

  if (err != NULL) {
    err_h root = SE_get_error_root(err);
    uint16_t tag = root ? (uint16_t)root->tag : 0;
    uint16_t owner = root ? (uint16_t)root->owner : 0;
    rsp->buf[2] = SYS_INTERFACE_STATUS_ERROR;
    rsp->buf[3] = (uint8_t)(tag & 0xFF);
    rsp->buf[4] = (uint8_t)(tag >> 8);
    rsp->buf[5] = (uint8_t)(owner & 0xFF);
    rsp->buf[6] = (uint8_t)(owner >> 8);
    rsp->len = 7;
  }

  SE_REPORT(sys_interface_send(rsp->buf, rsp->len));
  SE_REPORT(err);
}

#undef OWNER
#define OWNER OWNER_SYS_INTERFACE_CLASS
err_h sys_interface_register_decoder(uint8_t decoder_header, sys_interface_handler_f decoder, const char* name) {
  SE_CHECK_NOT_NULL(decoder);

  for (size_t i = 0; i < s_decoder_count; i++) {
    if (s_decoders[i].decoder_header == decoder_header) {
      SE_FAIL(ERR_INTERFACE_CLASS_TAKEN, .class_header = decoder_header);
    }
  }
  if (s_decoder_count >= CONFIG_SYS_INTERFACE_MAX_CLASSES) {
    SE_FAIL(ERR_INTERFACE_NO_CLASS_SLOTS, .class_header = decoder_header);
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
  s_tap_buff = (sys_buff_t){
      .buff = s_tap_ringbuffer,
      .truncated = 0,
  };
  sys_data_connector_t* conn = sys_data_connector_get(SYS_INTERFACE_CONNECTOR_ID);
  if (!conn) {
    SE_FAIL(ERR_BASE_NO_MEM, 0);
  }

  if (s_interface_rx_task_handle == NULL) {
    R_TASK_START(s_interface_rx_task_handle, sys_interface_receiver_task, NULL, CONFIG_SYS_INTERFACE_RX_TASK_PRIO);
    if (s_interface_rx_task_handle == NULL) {
      SE_FAIL(ERR_BASE_NO_MEM, 0);
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
    SE_FAIL(ERR_BASE_NOT_FOUND, 0);
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
        SE_REPORT(dq_err);
        break;
      }
      if (len == 0) break;

      if (s_tap_capture) {
        SE_REPORT(sys_buff_push(&s_tap_buff, s_rx_frame, len, 0));
      }
      decode_live_frame(s_rx_frame, len);
    }
  }
}

#undef OWNER

err_h sys_interface_handle_fault(err_h node, err_h chain) {
  (void)chain;
  if (!node || SE_get_tag_level(node->tag) != SE_LEVEL_CRITICAL) {
    return NULL;
  }
  // Severe interface fault: suspend RX to prevent corrupt frame flooding
  sys_interface_suspend_rx();
  return NULL;
}

