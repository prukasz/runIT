#include "sys_settings.h"
#include <nvs.h>
#include <nvs_flash.h>

#define SYS_SETTINGS_NVS_NAMESPACE "settings"

static nvs_handle_t s_nvs = 0;
static bool s_ready = false;

#undef OWNER
#define OWNER OWNER_SYS_SETTINGS_INIT
err_h sys_settings_init(void) {
  if (s_ready) return NULL;
  SE_TRY_ESP(nvs_flash_init());
  SE_TRY_ESP(nvs_open(SYS_SETTINGS_NVS_NAMESPACE, NVS_READWRITE, &s_nvs));
  s_ready = true;
  return NULL;
}

#undef OWNER
#define OWNER OWNER_SYS_SETTINGS_LOAD
err_h sys_settings_load(const char* key, void* out, size_t size, bool* out_found) {
  SE_CHECK_NOT_NULL(key);
  SE_CHECK_NOT_NULL(out);
  SE_CHECK_NOT_NULL(out_found);
  *out_found = false;
  if (!s_ready) SE_FAIL(ERR_BASE_INVALID_STATE, 0);

  size_t stored = 0;
  esp_err_t rc = nvs_get_blob(s_nvs, key, NULL, &stored);
  if (rc == ESP_ERR_NVS_NOT_FOUND) return NULL;
  SE_TRY_ESP(rc);
  if (stored != size) {
    SE_FAIL(ERR_SETTINGS_SIZE_MISMATCH, .stored = (uint16_t)stored, .expected = (uint16_t)size);
  }
  SE_TRY_ESP(nvs_get_blob(s_nvs, key, out, &stored));
  *out_found = true;
  return NULL;
}

#undef OWNER
#define OWNER OWNER_SYS_SETTINGS_STORE
err_h sys_settings_store(const char* key, const void* data, size_t size) {
  SE_CHECK_NOT_NULL(key);
  SE_CHECK_NOT_NULL(data);
  if (!s_ready) SE_FAIL(ERR_BASE_INVALID_STATE, 0);
  SE_TRY_ESP(nvs_set_blob(s_nvs, key, data, size));
  SE_TRY_ESP(nvs_commit(s_nvs));
  return NULL;
}

#undef OWNER
#define OWNER OWNER_SYS_SETTINGS_ERASE
err_h sys_settings_erase(const char* key) {
  SE_CHECK_NOT_NULL(key);
  if (!s_ready) SE_FAIL(ERR_BASE_INVALID_STATE, 0);
  esp_err_t rc = nvs_erase_key(s_nvs, key);
  if (rc != ESP_ERR_NVS_NOT_FOUND) SE_TRY_ESP(rc);
  SE_TRY_ESP(nvs_commit(s_nvs));
  return NULL;
}
