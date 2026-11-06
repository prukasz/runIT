#include "sys_nvs.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_rom_crc.h"
#include "nvs_flash.h"
#include "utils.h"

#define TAG __FILE_NAME__

#define KEY_INFO "info"
#define KEY_LEN  7u  // "c" + a u16 in decimal + NUL

U_MUTEX_DEFINE(sys_nvs_mutex);  // the files' info: both router tasks write

err_h sys_nvs_init(const char* partition) {
  esp_err_t rc = nvs_flash_init_partition(partition);
  if (rc == ESP_ERR_NVS_NO_FREE_PAGES || rc == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_LOGW(TAG, "partition '%s' unusable (%s): erasing", partition, esp_err_to_name(rc));
    ERR_TRY_ESP(nvs_flash_erase_partition(partition));
    rc = nvs_flash_init_partition(partition);
  }
  return ERR_ESP(rc);
}

static void chunk_key(uint16_t chunk, char key[KEY_LEN]) {
  snprintf(key, KEY_LEN, "c%u", (unsigned)chunk);
}

err_h sys_nvs_json_open(sys_nvs_json_t* file, const char* partition, const char* name) {
  memset(file, 0, sizeof(*file));
  ERR_TRY_ESP(nvs_open_from_partition(partition, name, NVS_READWRITE, &file->nvs));
  size_t size = sizeof(file->info);
  const esp_err_t rc = nvs_get_blob(file->nvs, KEY_INFO, &file->info, &size);
  if (rc == ESP_ERR_NVS_NOT_FOUND) {
    return NULL;  // an empty file
  }
  return ERR_ESP(rc);
}

// The chunk first: the info is what makes it part of the file.
static err_h append_locked(sys_nvs_json_t* file, uint32_t offset, const uint8_t* data, uint32_t len) {
  ERR_CHECK(offset == file->info.bytes, ERR_NVS_OFFSET, .got = offset, .want = file->info.bytes);
  ERR_CHECK(file->info.chunks < UINT16_MAX, ERR_NVS_BAD_ARG);
  char key[KEY_LEN];
  chunk_key(file->info.chunks, key);
  ERR_TRY_ESP(nvs_set_blob(file->nvs, key, data, len));
  const sys_nvs_json_info_t next = {.bytes = file->info.bytes + len,
                                    .crc32 = esp_rom_crc32_le(file->info.crc32, data, len),
                                    .chunks = file->info.chunks + 1u};
  ERR_TRY_ESP(nvs_set_blob(file->nvs, KEY_INFO, &next, sizeof(next)));
  ERR_TRY_ESP(nvs_commit(file->nvs));
  file->info = next;
  return NULL;
}

err_h sys_nvs_json_append(sys_nvs_json_t* file, uint32_t offset, const uint8_t* data, uint32_t len) {
  ERR_CHECK(data != NULL && len > 0, ERR_NVS_BAD_ARG);
  (void)xSemaphoreTake(sys_nvs_mutex, portMAX_DELAY);
  err_h err = append_locked(file, offset, data, len);
  (void)xSemaphoreGive(sys_nvs_mutex);
  return err;
}

static err_h erase_locked(sys_nvs_json_t* file) {
  ERR_TRY_ESP(nvs_erase_all(file->nvs));
  ERR_TRY_ESP(nvs_commit(file->nvs));
  memset(&file->info, 0, sizeof(file->info));
  return NULL;
}

err_h sys_nvs_json_erase(sys_nvs_json_t* file) {
  (void)xSemaphoreTake(sys_nvs_mutex, portMAX_DELAY);
  err_h err = erase_locked(file);
  (void)xSemaphoreGive(sys_nvs_mutex);
  return err;
}

static err_h get_locked(sys_nvs_json_t* file, uint16_t chunk, uint8_t* buf, uint32_t cap, uint32_t* len) {
  ERR_CHECK(chunk < file->info.chunks, ERR_NVS_NO_CHUNK, .chunk = chunk);
  char key[KEY_LEN];
  chunk_key(chunk, key);
  size_t size = cap;
  ERR_TRY_ESP(nvs_get_blob(file->nvs, key, buf, &size));
  *len = (uint32_t)size;
  return NULL;
}

err_h sys_nvs_json_get(sys_nvs_json_t* file, uint16_t chunk, uint8_t* buf, uint32_t cap, uint32_t* len) {
  (void)xSemaphoreTake(sys_nvs_mutex, portMAX_DELAY);
  err_h err = get_locked(file, chunk, buf, cap, len);
  (void)xSemaphoreGive(sys_nvs_mutex);
  return err;
}

sys_nvs_json_info_t sys_nvs_json_info(sys_nvs_json_t* file) {
  (void)xSemaphoreTake(sys_nvs_mutex, portMAX_DELAY);
  const sys_nvs_json_info_t info = file->info;
  (void)xSemaphoreGive(sys_nvs_mutex);
  return info;
}
