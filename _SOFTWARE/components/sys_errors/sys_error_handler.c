#include "sys_error.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <string.h>
#include "enc_sys_errors.h"
#include "sys_ble.h"
#include "utils.h"

// enc_sys_errors.h leaves OWNER set to OWNER_ENC_SYS_ERRORS; take it back so
// this file's own SE_* macros (if any are added later) are tagged correctly.
#undef OWNER
#define OWNER OWNER_SYS_ERRORS_BASE

static const char* TAG = __FILE_NAME__;

#define ERR_RECORD_POOL_SIZE 16
#define SE_RECORD_PAYLOAD_BUF_SIZE 384

typedef struct se_record {
  uint8_t storage[SE_RECORD_PAYLOAD_BUF_SIZE] __attribute__((aligned(8)));
  uint16_t used_bytes;
  uint8_t node_count;
  uint8_t total_depth;
  bool truncated;
} se_record_t;

static se_record_t s_record_pool[ERR_RECORD_POOL_SIZE];
R_QUEUE_DEFINE(s_free_queue, ERR_RECORD_POOL_SIZE, sizeof(se_record_t*));
R_QUEUE_DEFINE(s_err_queue, ERR_RECORD_POOL_SIZE, sizeof(se_record_t*));

static volatile uint32_t s_dropped_count = 0;

static void init_record_pool(void) {
  static bool inited = false;
  if (inited) return;
  inited = true;
  for (int i = 0; i < ERR_RECORD_POOL_SIZE; i++) {
    se_record_t* rec = &s_record_pool[i];
    (void)R_QUEUE_SEND(s_free_queue, &rec, NO_WAIT);
  }
}

__attribute__((constructor)) static void se_record_pool_ctor(void) {
  init_record_pool();
}

uint32_t SE_get_dropped_count(void) {
  return __atomic_load_n(&s_dropped_count, __ATOMIC_RELAXED);
}

void SE_clear_dropped_count(void) {
  __atomic_store_n(&s_dropped_count, 0, __ATOMIC_RELAXED);
}

#define ERR_HANDLER_TASK_STACK_WORDS 4096
R_TASK_DEFINE(s_err_handler_task_handle, ERR_HANDLER_TASK_STACK_WORDS);

static se_device_error_hook_t s_device_error_hook = NULL;

void SE_register_device_error_hook(se_device_error_hook_t hook) {
  s_device_error_hook = hook;
}

// Basic detection: the first ERR_DEV_DEP_FAILED node whose owner is in the
// device-provider range (devices_owners.h's PROVIDER_OWNER_MAP, 0xD0xx) is
// treated as "the device that generated this chain" and dispatched once via
// the registered hook - ERR_DEV_DEP_FAILED is the only device-raised tag
// guaranteed to carry a dev_id payload (see RET_IF_DEV_ERR in sys_device.h).
// Once found, the rest of the chain is skipped for this purpose; printing/
// sending below is unaffected and still covers every node regardless.
static void dispatch_device_owned_error(err_h err_chain) {
  if (!s_device_error_hook) return;
  /* A response failure can retain the action/callback failure as its cause.
     Never feed that diagnostic back into the same device policy. */
  if (err_chain && err_chain->tag == ERR_DEV_FAULT_RESPONSE_FAILED) return;
  for (err_h curr = err_chain; curr != NULL; curr = curr->next_cause) {
    if (curr->tag == ERR_DEV_DEP_FAILED && (curr->owner & 0xFF00) == (OWNER_DEVICE_BASE & 0xFF00)) {
      uint8_t dev_id = ((err_payload_ERR_DEV_DEP_FAILED_t*)curr->payload)->dev_id;
      err_h response_error = s_device_error_hook(dev_id, err_chain);
      if (response_error) SE_push_to_handler(response_error);
      break;
    }
  }
}

static bool se_record_copy_chain(se_record_t* rec, err_h chain) {
  if (!rec || !chain) return false;

  rec->node_count = 0;
  rec->total_depth = 0;
  rec->used_bytes = 0;
  rec->truncated = false;

  sys_err_t* prev_copy = NULL;
  uint32_t offset = 0;

  for (err_h curr = chain; curr != NULL && rec->total_depth < ENC_SYS_ERRORS_MAX_NODES; curr = curr->next_cause) {
    rec->total_depth++;

    if (rec->truncated) {
      continue;
    }

    size_t payload_size = SE_get_payload_size(curr->tag);
    size_t node_size = sizeof(sys_err_t) + payload_size;
    node_size = (node_size + 7) & ~7u;  // 8-byte align

    if (offset + node_size > SE_RECORD_PAYLOAD_BUF_SIZE) {
      rec->truncated = true;
      continue;
    }

    sys_err_t* node_copy = (sys_err_t*)&rec->storage[offset];
    node_copy->tag = curr->tag;
    node_copy->owner = curr->owner;
    node_copy->next_cause = NULL;
    if (payload_size > 0) {
      memcpy(node_copy->payload, curr->payload, payload_size);
    }

    if (prev_copy != NULL) {
      prev_copy->next_cause = node_copy;
    }
    prev_copy = node_copy;

    offset += node_size;
    rec->node_count++;
  }

  rec->used_bytes = (uint16_t)offset;
  return rec->node_count > 0;
}

static void sys_error_handler_task(void* arg) {
  (void)arg;
  se_record_t* rec = NULL;
  uint8_t packet[SE_ERR_PACKET_MAX];
  sys_error_cfg_t cfg;

  while (1) {
    if (!R_QUEUE_RECEIVE(s_err_queue, &rec, WAIT_FOREVER)) continue;
    if (!rec) continue;

    uint32_t dropped = __atomic_exchange_n(&s_dropped_count, 0, __ATOMIC_RELAXED);
    if (dropped > 0) {
      ESP_LOGW(TAG, "Error queue overflow: %u error chains were dropped", (unsigned)dropped);
    }

    err_h err_chain = (err_h)rec->storage;

    SE_get_config(&cfg);
    dispatch_device_owned_error(err_chain);

    // Encode first: telemetry serialization is self-contained.
    size_t packet_len = 0;
    bool encoded = false;
    if (cfg.errors.ble_enable) {
      encoded = SE_IS_OK(enc_sys_errors_encode_chain(err_chain, packet, cfg.errors.packet_max, &packet_len));
    }

    if (cfg.errors.serial_trace) {
      ESP_LOGE(TAG, "========== ERROR STACK TRACE ==========");
      int depth = 0;
      for (err_h curr = err_chain; curr != NULL; curr = curr->next_cause) {
        // Payload printed generically as hex whenever the tag has one - a
        // per-tag pretty-printer lives closer to whoever cares about a
        // specific tag (e.g. adapter_pca9685.c's explain_root_cause()),
        // not here; this is just "show whatever bytes exist, if any".
        char desc[96] = {0};
        if (SE_describe_payload(curr->tag, curr->payload, desc, sizeof(desc))) {
          ESP_LOGE(TAG, "  [%d] Owner: %s (0x%04X), Tag: %s (%d) -> %s", depth++, SE_get_owner_name(curr->owner), (unsigned int)curr->owner, SE_get_tag_name(curr->tag), (int)curr->tag, desc);
        } else {
          size_t psize = SE_get_payload_size(curr->tag);
          if (psize > 0) {
            char hex[3 * 16 + 1] = {0};
            size_t show = psize > 16 ? 16 : psize;
            for (size_t b = 0; b < show; b++) {
              snprintf(&hex[b * 3], 4, "%02X ", curr->payload[b]);
            }
            ESP_LOGE(TAG, "  [%d] Owner: %s (0x%04X), Tag: %s (%d), Payload[%u]: %s%s", depth++, SE_get_owner_name(curr->owner), (unsigned int)curr->owner, SE_get_tag_name(curr->tag), (int)curr->tag, (unsigned)psize, hex, psize > 16 ? "..." : "");
          } else {
            ESP_LOGE(TAG, "  [%d] Owner: %s (0x%04X), Tag: %s (%d)", depth++, SE_get_owner_name(curr->owner), (unsigned int)curr->owner, SE_get_tag_name(curr->tag), (int)curr->tag);
          }
        }
      }
      ESP_LOGE(TAG, "=======================================");
    }

    if (encoded) {
      (void)sys_ble_char_send(cfg.errors.char_uuid, cfg.errors.tx_header, packet, packet_len, true);
    }

    // Release record back to free pool
    (void)R_QUEUE_SEND(s_free_queue, &rec, NO_WAIT);
  }
}

void SE_init(void) {
  init_record_pool();
  if (s_err_handler_task_handle == NULL) {
    R_TASK_START(s_err_handler_task_handle, sys_error_handler_task, NULL, 5);
  }
}

void SE_push_to_handler(err_h err) {
  if (!err || SE_is_suspended()) return;

  init_record_pool();

  se_record_t* rec = NULL;
  bool is_isr = xPortInIsrContext();

  if (is_isr) {
    BaseType_t woken = pdFALSE;
    if (!xQueueReceiveFromISR(s_free_queue, &rec, &woken)) {
      __atomic_fetch_add(&s_dropped_count, 1, __ATOMIC_RELAXED);
      return;
    }
    if (woken) {
      portYIELD_FROM_ISR();
    }
  } else {
    if (!R_QUEUE_RECEIVE(s_free_queue, &rec, NO_WAIT)) {
      __atomic_fetch_add(&s_dropped_count, 1, __ATOMIC_RELAXED);
      return;
    }
  }

  if (!se_record_copy_chain(rec, err)) {
    if (is_isr) {
      R_QUEUE_SEND_ISR(s_free_queue, &rec);
    } else {
      (void)R_QUEUE_SEND(s_free_queue, &rec, NO_WAIT);
    }
    __atomic_fetch_add(&s_dropped_count, 1, __ATOMIC_RELAXED);
    return;
  }

  bool sent = false;
  if (is_isr) {
    BaseType_t woken = pdFALSE;
    sent = (xQueueSendFromISR(s_err_queue, &rec, &woken) == pdTRUE);
    if (woken) {
      portYIELD_FROM_ISR();
    }
  } else {
    sent = (R_QUEUE_SEND(s_err_queue, &rec, NO_WAIT) == pdTRUE);
  }

  if (!sent) {
    if (is_isr) {
      R_QUEUE_SEND_ISR(s_free_queue, &rec);
    } else {
      (void)R_QUEUE_SEND(s_free_queue, &rec, NO_WAIT);
    }
    __atomic_fetch_add(&s_dropped_count, 1, __ATOMIC_RELAXED);
  }
}
