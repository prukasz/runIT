/**
 * @file nvs_store_esp.c
 * @brief The store (nvs_store.h) of this chip: ESP-IDF's nvs_flash. The one file that names it.
 */

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "nvs_store.h"

#define TAG __FILE_NAME__

_Static_assert(sizeof(nvs_handle_t) == sizeof(nvs_store_t), "a namespace is ESP-IDF's handle");

// ESP-IDF's "not found" as the store's tag, any other code as the core's ERR_ESP_ERR.
static err_h check(esp_err_t rc) {
  ERR_CHECK(rc != ESP_ERR_NVS_NOT_FOUND, ERR_NVS_NOT_FOUND);
  return ERR_ESP(rc);
}

err_h nvs_store_init(const char* partition) {
  esp_err_t rc = nvs_flash_init_partition(partition);
  if (rc == ESP_ERR_NVS_NO_FREE_PAGES || rc == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_LOGW(TAG, "partition '%s' unusable (%s): erasing", partition, esp_err_to_name(rc));
    ERR_TRY_ESP(nvs_flash_erase_partition(partition));
    rc = nvs_flash_init_partition(partition);
  }
  return ERR_ESP(rc);
}

err_h nvs_store_open(const char* partition, const char* name, nvs_store_t* ns) {
  nvs_handle_t h = 0;
  ERR_TRY_ESP(nvs_open_from_partition(partition, name, NVS_READWRITE, &h));
  *ns = (nvs_store_t)h;
  return NULL;
}

err_h nvs_store_get(nvs_store_t ns, const char* key, void* buf, uint32_t* len) {
  size_t size = *len;
  ERR_TRY(check(nvs_get_blob((nvs_handle_t)ns, key, buf, &size)));
  *len = (uint32_t)size;
  return NULL;
}

err_h nvs_store_set(nvs_store_t ns, const char* key, const void* data, uint32_t len) {
  ERR_TRY_ESP(nvs_set_blob((nvs_handle_t)ns, key, data, len));
  return ERR_ESP(nvs_commit((nvs_handle_t)ns));
}

err_h nvs_store_erase(nvs_store_t ns, const char* key) {
  ERR_TRY(check(key != NULL ? nvs_erase_key((nvs_handle_t)ns, key) : nvs_erase_all((nvs_handle_t)ns)));
  return ERR_ESP(nvs_commit((nvs_handle_t)ns));
}

err_h nvs_store_keys(nvs_store_t ns, nvs_store_key_f fn, void* ctx) {
  nvs_iterator_t it = NULL;
  esp_err_t rc = nvs_entry_find_in_handle((nvs_handle_t)ns, NVS_TYPE_ANY, &it);
  while (rc == ESP_OK) {
    nvs_entry_info_t info;
    rc = nvs_entry_info(it, &info);
    if (rc != ESP_OK) {
      break;
    }
    fn(info.key, ctx);
    rc = nvs_entry_next(&it);
  }
  nvs_release_iterator(it);
  return rc == ESP_ERR_NVS_NOT_FOUND ? NULL : ERR_ESP(rc);
}
