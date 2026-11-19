/**
 * @file nvs_store_json.c
 * @brief A JSON file of chunks (nvs_store_json.h), through the store's interface (`nvs_store`) only; and the module's
 * error tags registered, which every implementation raises. The store itself is esp/.
 */

#include "nvs_store_json.h"

#include <stdio.h>
#include <string.h>

#include "utils.h"

ERR_MODULE_DEFINE(MODULE_ID_NVS, ERR_NVS_STORE_MAP)

#define KEY_INFO "info"
#define KEY_LEN  7u  // "c" + a u16 in decimal + NUL

U_MUTEX_DEFINE(nvs_store_mutex);  // the files' info: both router tasks write

// CRC-32 (zlib / IEEE, reflected 0xEDB88320) of `len` bytes, going on from `crc`.
static uint32_t crc32(uint32_t crc, const uint8_t* data, uint32_t len) {
  crc = ~crc;
  for (uint32_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint32_t b = 0; b < 8u; b++) {
      crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(crc & 1u));
    }
  }
  return ~crc;
}

static void chunk_key(uint16_t chunk, char key[KEY_LEN]) {
  snprintf(key, KEY_LEN, "c%u", (unsigned)chunk);
}

err_h nvs_store_json_open(nvs_store_json_t* file, const char* partition, const char* name) {
  memset(file, 0, sizeof(*file));
  ERR_TRY(nvs_store.open(partition, name, &file->ns));
  uint32_t size = sizeof(file->info);
  err_h err = nvs_store.get(file->ns, KEY_INFO, &file->info, &size);
  if (err != NULL && err_tag(err) == ERR_NVS_NOT_FOUND) {
    err_free(err);
    return NULL;  // an empty file
  }
  return err;
}

// The chunk first: the info is what makes it part of the file.
static err_h append_locked(nvs_store_json_t* file, uint32_t offset, const uint8_t* data, uint32_t len) {
  ERR_CHECK(offset == file->info.bytes, ERR_NVS_OFFSET, .got = offset, .want = file->info.bytes);
  ERR_CHECK(file->info.chunks < UINT16_MAX, ERR_NVS_BAD_ARG);
  char key[KEY_LEN];
  chunk_key(file->info.chunks, key);
  ERR_TRY(nvs_store.set(file->ns, key, data, len));
  const nvs_store_json_info_t next = {.bytes = file->info.bytes + len,
                                      .crc32 = crc32(file->info.crc32, data, len),
                                      .chunks = file->info.chunks + 1u};
  ERR_TRY(nvs_store.set(file->ns, KEY_INFO, &next, sizeof(next)));
  file->info = next;
  return NULL;
}

err_h nvs_store_json_append(nvs_store_json_t* file, uint32_t offset, const uint8_t* data, uint32_t len) {
  ERR_CHECK(data != NULL && len > 0, ERR_NVS_BAD_ARG);
  (void)xSemaphoreTake(nvs_store_mutex, portMAX_DELAY);
  err_h err = append_locked(file, offset, data, len);
  (void)xSemaphoreGive(nvs_store_mutex);
  return err;
}

err_h nvs_store_json_erase(nvs_store_json_t* file) {
  (void)xSemaphoreTake(nvs_store_mutex, portMAX_DELAY);
  err_h err = nvs_store.erase(file->ns, NULL);
  if (err == NULL) {
    memset(&file->info, 0, sizeof(file->info));
  }
  (void)xSemaphoreGive(nvs_store_mutex);
  return err;
}

static err_h get_locked(nvs_store_json_t* file, uint16_t chunk, uint8_t* buf, uint32_t cap, uint32_t* len) {
  ERR_CHECK(chunk < file->info.chunks, ERR_NVS_NO_CHUNK, .chunk = chunk);
  char key[KEY_LEN];
  chunk_key(chunk, key);
  *len = cap;
  return nvs_store.get(file->ns, key, buf, len);
}

err_h nvs_store_json_get(nvs_store_json_t* file, uint16_t chunk, uint8_t* buf, uint32_t cap, uint32_t* len) {
  (void)xSemaphoreTake(nvs_store_mutex, portMAX_DELAY);
  err_h err = get_locked(file, chunk, buf, cap, len);
  (void)xSemaphoreGive(nvs_store_mutex);
  return err;
}

nvs_store_json_info_t nvs_store_json_info(nvs_store_json_t* file) {
  (void)xSemaphoreTake(nvs_store_mutex, portMAX_DELAY);
  const nvs_store_json_info_t info = file->info;
  (void)xSemaphoreGive(nvs_store_mutex);
  return info;
}
