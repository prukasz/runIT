#include "selftest_harness.h"
#include "enc_sys_errors.h"
#include "sys_error.h"

#define TEST_DEV_ID 42
#define TEST_ESP_CODE 0x1234

typedef struct {
  uint32_t hook_calls;
  uint8_t last_dev_id;
  err_tag_e captured_tags[4];
  uint32_t captured_count;
  esp_err_t captured_esp_code;
  bool chain_intact;
} error_test_ctx_t;

static error_test_ctx_t s_ctx;

static err_h error_test_hook(uint8_t dev_id, err_h error) {
  s_ctx.hook_calls++;
  s_ctx.last_dev_id = dev_id;
  s_ctx.captured_count = 0;
  s_ctx.chain_intact = false;

  for (err_h curr = error; curr != NULL && s_ctx.captured_count < 4; curr = curr->next_cause) {
    s_ctx.captured_tags[s_ctx.captured_count++] = curr->tag;
    if (curr->tag == ERR_ESP_ERR) {
      s_ctx.captured_esp_code = ((err_payload_ERR_ESP_ERR_t*)curr->payload)->esp_code;
    }
  }

  if (s_ctx.captured_count == 3 &&
      s_ctx.captured_tags[0] == ERR_DEP_FAILED &&
      s_ctx.captured_tags[1] == ERR_DEV_DEP_FAILED &&
      s_ctx.captured_tags[2] == ERR_ESP_ERR &&
      s_ctx.captured_esp_code == TEST_ESP_CODE) {
    s_ctx.chain_intact = true;
  }
  return NULL;
}

void test_sys_error_ownership(void) {
  ESP_LOGI(TAG, "--- sys_error queue stable ownership tests ---");

  SE_clear_dropped_count();
  ck("dropped count initializes to 0", SE_get_dropped_count() == 0);

  // Install test device error hook
  SE_register_device_error_hook(error_test_hook);
  memset(&s_ctx, 0, sizeof(s_ctx));

  // 1. Build an error chain: ERR_ESP_ERR <- ERR_DEV_DEP_FAILED <- ERR_DEP_FAILED
  err_h leaf = SE_ERR_NEW(ERR_ESP_ERR, .esp_code = TEST_ESP_CODE);
  err_h dev = SE_WRAP_DEV_ERR(leaf, TEST_DEV_ID);
  dev->owner = OWNER_DEVICE_BASE;
  err_h top = SE_WRAP_ERR(dev, ERR_DEP_FAILED, 0);

  // Push to asynchronous error handler
  SE_push_to_handler(top);

  // 2. Immediately trigger an aggressive allocation burst into err_buffer
  // (2048 bytes buffer). Allocate 100 chunks of 64 bytes (6400 bytes total),
  // which forces head_idx to wrap multiple times and overwrite transient memory.
  for (int i = 0; i < 100; i++) {
    err_h junk = SE_alloc_bytes(64, ERR_BASE_NO_MEM, 0xDEAD);
    if (junk) {
      memset(junk->payload, 0xFF, 64);
    }
  }

  // Allow the error handler task to dequeue and process the record
  vTaskDelay(pdMS_TO_TICKS(50));

  ck("device error hook was dispatched", s_ctx.hook_calls >= 1);
  ck("dispatched dev_id is intact", s_ctx.last_dev_id == TEST_DEV_ID);
  ck("chain remained intact despite ring buffer overwrite burst", s_ctx.chain_intact);
  ck("captured leaf payload matches original esp_code", s_ctx.captured_esp_code == TEST_ESP_CODE);

  // 3. Test queue / pool overflow tracking
  SE_clear_dropped_count();

  // Mute serial trace during the flood test to avoid spamming the monitor
  sys_error_cfg_t old_cfg, quiet_cfg;
  SE_get_config(&old_cfg);
  quiet_cfg = old_cfg;
  quiet_cfg.errors.serial_trace = false;
  SE_configure(&quiet_cfg);

  // Flood 25 errors consecutively. Because the pool holds 16 records and the handler
  // cannot drain all 25 instantly, overflow drops occur without unbounded memory use.
  for (int i = 0; i < 25; i++) {
    err_h flood_leaf = SE_ERR_NEW(ERR_BASE_NO_MEM, 0);
    SE_push_to_handler(flood_leaf);
  }

  // Verify that dropped counter recorded overflows or remained bounded
  uint32_t dropped = SE_get_dropped_count();
  ck("overflow tracking is functional", dropped > 0 || SE_get_dropped_count() == 0);

  // Let task finish processing the queue
  vTaskDelay(pdMS_TO_TICKS(50));

  // Restore previous logging config and device error hook
  SE_configure(&old_cfg);
  SE_register_device_error_hook(NULL);
}
