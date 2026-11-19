#include "nvs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_rom_crc.h"

#define MAX_ENTRIES 64

typedef struct {
  bool used;
  char key[16];
  uint8_t* data;
  size_t len;
} entry_t;

struct nvs_iterator_s {
  uint32_t next;  /* the entry to look at next */
  uint32_t at;    /* the entry nvs_entry_info reports */
};

static entry_t s_e[MAX_ENTRIES];
static uint32_t s_writes;

uint32_t esp_rom_crc32_le(uint32_t crc, const uint8_t* buf, uint32_t len) {
  crc = ~crc;
  for (uint32_t i = 0; i < len; i++) {
    crc ^= buf[i];
    for (int b = 0; b < 8; b++) {
      crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
  }
  return ~crc;
}

static entry_t* find(const char* key) {
  for (uint32_t i = 0; i < MAX_ENTRIES; i++) {
    if (s_e[i].used && strcmp(s_e[i].key, key) == 0) {
      return &s_e[i];
    }
  }
  return NULL;
}

void host_nvs_reset(void) {
  for (uint32_t i = 0; i < MAX_ENTRIES; i++) {
    free(s_e[i].data);
  }
  memset(s_e, 0, sizeof(s_e));
  s_writes = 0;
}

uint32_t host_nvs_writes(void) {
  return s_writes;
}

uint32_t host_nvs_count(void) {
  uint32_t n = 0;
  for (uint32_t i = 0; i < MAX_ENTRIES; i++) {
    n += s_e[i].used ? 1u : 0u;
  }
  return n;
}

esp_err_t nvs_open_from_partition(const char* partition, const char* ns, nvs_open_mode_t mode, nvs_handle_t* out) {
  (void)partition;
  (void)ns;
  (void)mode;
  *out = 1;
  return ESP_OK;
}

esp_err_t nvs_set_blob(nvs_handle_t h, const char* key, const void* value, size_t len) {
  (void)h;
  entry_t* e = find(key);
  if (e == NULL) {
    for (uint32_t i = 0; i < MAX_ENTRIES && e == NULL; i++) {
      e = s_e[i].used ? NULL : &s_e[i];
    }
  }
  if (e == NULL) {
    return ESP_ERR_NO_MEM;
  }
  uint8_t* copy = malloc(len);
  if (copy == NULL) {
    return ESP_ERR_NO_MEM;
  }
  memcpy(copy, value, len);
  free(e->data);
  e->used = true;
  snprintf(e->key, sizeof(e->key), "%s", key);
  e->data = copy;
  e->len = len;
  s_writes++;
  return ESP_OK;
}

esp_err_t nvs_get_blob(nvs_handle_t h, const char* key, void* out, size_t* len) {
  (void)h;
  entry_t* e = find(key);
  if (e == NULL) {
    return ESP_ERR_NVS_NOT_FOUND;
  }
  if (out != NULL) {
    memcpy(out, e->data, *len < e->len ? *len : e->len);
  }
  *len = e->len;
  return ESP_OK;
}

esp_err_t nvs_erase_key(nvs_handle_t h, const char* key) {
  (void)h;
  entry_t* e = find(key);
  if (e == NULL) {
    return ESP_ERR_NVS_NOT_FOUND;
  }
  free(e->data);
  memset(e, 0, sizeof(*e));
  return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t h) {
  (void)h;
  return ESP_OK;
}

static esp_err_t seek(nvs_iterator_t* it) {
  while ((*it)->next < MAX_ENTRIES && !s_e[(*it)->next].used) {
    (*it)->next++;
  }
  if ((*it)->next >= MAX_ENTRIES) {
    free(*it);
    *it = NULL;
    return ESP_ERR_NVS_NOT_FOUND;
  }
  (*it)->at = (*it)->next++;
  return ESP_OK;
}

esp_err_t nvs_entry_find_in_handle(nvs_handle_t h, nvs_type_t type, nvs_iterator_t* it) {
  (void)h;
  (void)type;
  *it = calloc(1, sizeof(**it));
  return seek(it);
}

esp_err_t nvs_entry_next(nvs_iterator_t* it) {
  return seek(it);
}

esp_err_t nvs_entry_info(nvs_iterator_t it, nvs_entry_info_t* info) {
  memset(info, 0, sizeof(*info));
  snprintf(info->key, sizeof(info->key), "%s", s_e[it->at].key);
  info->type = NVS_TYPE_BLOB;
  return ESP_OK;
}

void nvs_release_iterator(nvs_iterator_t it) {
  free(it);
}
