#include "sys_ota.h"

#include "cmd.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "soc/gpio_num.h"
#include "utils.h"




ERR_MODULE_DEFINE(ERR_MOD_SYS_OTA, "ota", ERR_SYS_OTA_MAP)

U_MUTEX_DEFINE(sys_ota_mutex);  // s_ota: both router tasks

static struct {

  esp_ota_handle_t handle;
  const esp_partition_t* target;  // the slot being written, NULL: none
  uint32_t size;
  uint32_t written;
  sys_ota_busy_f busy;
} s_ota;

// Lock held.
static void close_write(void) {
  if (s_ota.target != NULL) {
    (void)esp_ota_abort(s_ota.handle);  // a handle that failed is gone already: nothing more to do with either answer
  }
  s_ota.target = NULL;
  s_ota.written = 0;
  s_ota.size = 0;
}

void sys_ota_set_guard(sys_ota_busy_f busy) {
  s_ota.busy = busy;
}



// Lock held.
static err_h begin(uint32_t size) {
  close_write();
  const esp_partition_t* target = esp_ota_get_next_update_partition(NULL);
  ERR_CHECK(target != NULL, ERR_OTA_TOO_BIG, .len = size, .max = 0);
  ERR_CHECK(size <= target->size, ERR_OTA_TOO_BIG, .len = size, .max = target->size);
  ERR_TRY_ESP(esp_ota_begin(target, OTA_WITH_SEQUENTIAL_WRITES, &s_ota.handle));  // erases as it goes: no long stall
  s_ota.target = target;
  s_ota.size = size;
  return NULL;
}

err_h sys_ota_begin(uint32_t size) {
  ERR_CHECK(size > 0, ERR_CMD_BAD_ARG);
  ERR_CHECK(s_ota.busy == NULL || !s_ota.busy(), ERR_OTA_BUSY);
  (void)xSemaphoreTake(sys_ota_mutex, portMAX_DELAY);
  err_h err = begin(size);
  (void)xSemaphoreGive(sys_ota_mutex);
  return err;
}

// Lock held.
static err_h write_chunk(uint32_t offset, const uint8_t* data, uint32_t len) {
  ERR_CHECK(s_ota.target != NULL, ERR_OTA_NOT_OPEN);
  ERR_CHECK(offset == s_ota.written, ERR_OTA_OFFSET, .got = offset, .want = s_ota.written);
  ERR_CHECK(offset + len <= s_ota.size, ERR_OTA_TOO_BIG, .len = offset + len, .max = s_ota.size);
  ERR_TRY_ESP(esp_ota_write(s_ota.handle, data, len));
  s_ota.written += len;
  return NULL;
}

err_h sys_ota_write(uint32_t offset, const uint8_t* data, uint32_t len) {
  ERR_CHECK(data != NULL && len > 0, ERR_CMD_BAD_ARG);
  (void)xSemaphoreTake(sys_ota_mutex, portMAX_DELAY);
  err_h err = write_chunk(offset, data, len);
  (void)xSemaphoreGive(sys_ota_mutex);
  return err;
}

// Lock held. esp_ota_end frees the handle whatever it answers.
static err_h finish(void) {
  ERR_CHECK(s_ota.target != NULL, ERR_OTA_NOT_OPEN);
  ERR_CHECK(s_ota.written == s_ota.size, ERR_OTA_INCOMPLETE, .got = s_ota.written, .want = s_ota.size);
  const esp_partition_t* target = s_ota.target;
  s_ota.target = NULL;
  const esp_err_t rc = esp_ota_end(s_ota.handle);
  ERR_CHECK(rc != ESP_ERR_OTA_VALIDATE_FAILED, ERR_OTA_BAD_IMAGE);
  ERR_TRY_ESP(rc);
  return ERR_ESP(esp_ota_set_boot_partition(target));
}

err_h sys_ota_commit(void) {
  (void)xSemaphoreTake(sys_ota_mutex, portMAX_DELAY);
  err_h err = finish();
  close_write();
  (void)xSemaphoreGive(sys_ota_mutex);
  return err;
}

void sys_ota_abort(void) {
  (void)xSemaphoreTake(sys_ota_mutex, portMAX_DELAY);
  close_write();
  (void)xSemaphoreGive(sys_ota_mutex);
}

static sys_ota_state_e state_of(const esp_partition_t* running) {
  esp_ota_img_states_t state;
  if (esp_ota_get_state_partition(running, &state) != ESP_OK) {
    return SYS_OTA_STATE_UNKNOWN;
  }
  switch (state) {
    case ESP_OTA_IMG_VALID:
      return SYS_OTA_STATE_VALID;
    case ESP_OTA_IMG_PENDING_VERIFY:
      return SYS_OTA_STATE_PENDING;
    default:
      return SYS_OTA_STATE_UNKNOWN;
  }
}

err_h sys_ota_confirm(void) {
  if (state_of(esp_ota_get_running_partition()) != SYS_OTA_STATE_PENDING) {
    return NULL;
  }
  return ERR_ESP(esp_ota_mark_app_valid_cancel_rollback());
}

sys_ota_info_t sys_ota_info(void) {
  const esp_partition_t* running = esp_ota_get_running_partition();
  const esp_partition_t* next = esp_ota_get_next_update_partition(NULL);
  sys_ota_info_t info = {
      .slot = (uint8_t)(running->subtype - ESP_PARTITION_SUBTYPE_APP_OTA_0),
      .state = (uint8_t)state_of(running),
      .slot_bytes = next != NULL ? next->size : 0,
  };
  (void)xSemaphoreTake(sys_ota_mutex, portMAX_DELAY);
  info.open = s_ota.target != NULL;
  info.written = s_ota.written;
  (void)xSemaphoreGive(sys_ota_mutex);
  return info;
}
