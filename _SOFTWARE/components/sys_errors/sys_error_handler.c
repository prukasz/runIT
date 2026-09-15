#include "sys_error.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <stdio.h>
#include "enc_sys_errors.h"
#include "utils.h"

// This file's DBG() calls fire on CONFIG_DBG_GLOBAL or this component's own
// switch (components/utils/Kconfig) - see DBG()'s doc comment in utils.h.
#define DBG_ENABLE CONFIG_DBG_ENABLE_SYS_ERRORS

// enc_sys_errors.h leaves OWNER set to OWNER_ENC_SYS_ERRORS; take it back so
// this file's own SE_* macros are tagged as sys_errors, not as the encoder.
#undef OWNER
#define OWNER OWNER_SYS_ERRORS_BASE

static const char* TAG = __FILE_NAME__;

#define ERR_QUEUE_LEN 32
R_QUEUE_DEFINE(s_err_queue, ERR_QUEUE_LEN, sizeof(err_h));

#define ERR_HANDLER_TASK_STACK_WORDS 4096
R_TASK_DEFINE(s_err_handler_task_handle, ERR_HANDLER_TASK_STACK_WORDS);

// -----------------------------------------------------------------------------
// Outbound Transport Sink
// -----------------------------------------------------------------------------

/* One slot, last registration wins - the mirror image of sys_interface's RX
   source registry (sys_interface_register_rx_source()): there, a transport
   hands the router a non-blocking dequeue callback so inbound frames find
   their way in; here, a transport hands the handler a send callback so
   outbound error packets find their way out. Neither side knows what the
   other transport is; sys_ble is reached only through the header-only
   binding in sys_error_config.h. */
static se_tx_sink_f s_tx_sink     = NULL;
static void*        s_tx_sink_ctx = NULL;

void SE_register_tx_sink(se_tx_sink_f send_fn, void* ctx, const char* name) {
  // Context first: the handler task reads the function pointer to decide
  // whether to send at all, so it must never find a new sink paired with the
  // previous one's context.
  s_tx_sink_ctx = ctx;
  s_tx_sink     = send_fn;

  DBG(ESP_LOGI(TAG, "error TX sink registered: %s", name ? name : (send_fn ? "unnamed" : "none")));
}

uint32_t SE_get_dropped_count(void) {
  return 0;
}

void SE_clear_dropped_count(void) {
}

// -----------------------------------------------------------------------------
// Handler Task
// -----------------------------------------------------------------------------

/* Restores the "serial stack trace" documented in SYS_ERRORS.MD: one decoded
   ESP_LOGE line per chain node (outermost first), falling back to a hex
   payload dump for tags with no registered logger. Bounded by
   ENC_SYS_ERRORS_MAX_NODES for the same reason the encoder is - the ring has
   no free, so a chain held too long can have next_cause overwritten into a
   cycle.

   Deliberately never writes to serial (or BLE) itself: ESP_LOGE goes through
   se_log_vprintf like any other log line, so it is carried wherever the
   `logs` routing currently in force (SE_configure()) already sends lines -
   mirror_on_serial and/or ble_enable - with no separate transport of its
   own. */
static void log_chain_to_serial(err_h chain) {
  char     desc[SE_LOG_LINE_MAX];
  uint32_t depth = 0;

  for (err_h node = chain; node != NULL && depth < ENC_SYS_ERRORS_MAX_NODES; node = node->next_cause, depth++) {
    if (!SE_describe_payload(node->tag, node->payload, desc, sizeof(desc))) {
      size_t  payload_len = SE_get_payload_size(node->tag);
      uint8_t dump_len    = (payload_len < 32u) ? (uint8_t)payload_len : 32u;
      size_t  pos         = 0;
      for (uint8_t i = 0; i < dump_len && pos + 3 < sizeof(desc); i++) {
        pos += (size_t)snprintf(desc + pos, sizeof(desc) - pos, "%02X ", node->payload[i]);
      }
      desc[pos] = '\0';
    }
    ESP_LOGE(TAG, "[%u] owner=%s (0x%04X) tag=%s (%d): %s", (unsigned)depth, SE_get_owner_name(node->owner), (unsigned)node->owner, SE_get_tag_name(node->tag), (int)node->tag, desc);
  }
}

static void sys_error_handler_task(void* arg) {
  (void)arg;
  err_h           err_chain = NULL;
  uint8_t         packet[SE_ERR_PACKET_MAX];
  sys_error_cfg_t cfg;

  while (1) {
    if (!R_QUEUE_RECEIVE(s_err_queue, &err_chain, WAIT_FOREVER)) continue;
    if (!err_chain) continue;

    SE_get_config(&cfg);

    /* Hex packet over the dedicated TX sink - one chain per packet. Skipped
       entirely (not even encoded) while no sink is bound; this is the only
       branch that touches the ring via encoding, so it must run before
       anything downstream gets a chance to allocate from it. */
    se_tx_sink_f sink = s_tx_sink;
    if (sink) {
      size_t packet_len = 0;
      if (SE_IS_OK(enc_sys_errors_encode_chain(err_chain, packet, cfg.errors.packet_max, &packet_len))) {
        /* The sink's own error is deliberately dropped rather than pushed
           back here - reporting a transport failure through the transport
           that just failed only loops. */
        (void)sink(s_tx_sink_ctx, cfg.errors.tx_header, packet, packet_len);
      }
    }

    /* Independent of the TX sink - a board with serial_trace on but no
       transport bound (or not yet bound) still gets the trace. */
    if (cfg.errors.serial_trace) {
      log_chain_to_serial(err_chain);
    }
  }
}

void SE_init(void) {
  if (s_err_handler_task_handle == NULL) {
    R_TASK_START(s_err_handler_task_handle, sys_error_handler_task, NULL, 5);
  }
}

void SE_push_to_handler(err_h err) {
  if (!err || SE_is_suspended()) return;

  if (xPortInIsrContext()) {
    R_QUEUE_SEND_ISR(s_err_queue, &err);
  } else {
    R_QUEUE_SEND(s_err_queue, &err, NO_WAIT);
  }
}
