#include "sys_error.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include "enc_sys_errors.h"
#include "sys_ble.h"
#include "utils.h"

// enc_sys_errors.h leaves OWNER set to OWNER_ENC_SYS_ERRORS; take it back so
// this file's own SE_* macros (if any are added later) are tagged correctly.
#undef OWNER
#define OWNER OWNER_SYS_ERRORS_BASE

static const char* TAG = __FILE_NAME__;

#define ERR_QUEUE_LEN 32
R_QUEUE_DEFINE(s_err_queue, ERR_QUEUE_LEN, sizeof(err_h));

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
  for (err_h curr = err_chain; curr != NULL; curr = curr->next_cause) {
    if (curr->tag == ERR_DEV_DEP_FAILED && (curr->owner & 0xFF00) == (OWNER_DEVICE_BASE & 0xFF00)) {
      uint8_t dev_id = ((err_payload_ERR_DEV_DEP_FAILED_t*)curr->payload)->dev_id;
      s_device_error_hook(dev_id, err_chain);
      break;
    }
  }
}

static void sys_error_handler_task(void* arg) {
  (void)arg;
  err_h err_chain = NULL;
  uint8_t packet[SE_ERR_PACKET_MAX];
  sys_error_cfg_t cfg;

  while (1) {
    if (!R_QUEUE_RECEIVE(s_err_queue, &err_chain, WAIT_FOREVER)) continue;
    if (!err_chain) continue;

    SE_get_config(&cfg);
    dispatch_device_owned_error(err_chain);

    // Encode first: everything below can allocate from the same ring the chain
    // lives in, and an allocation that wraps would overwrite nodes mid-walk.
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
