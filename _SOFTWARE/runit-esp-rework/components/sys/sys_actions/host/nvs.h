#pragma once
/* Host stand-in for ESP-IDF's nvs.h: the blob calls sys_actions uses, over a table in RAM that outlives
 * nvs_open_from_partition (so "a reboot" is a second sys_actions_init). host_nvs_* are the test's eyes and hands. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define ESP_ERR_NVS_NOT_FOUND 0x1102

typedef uint32_t nvs_handle_t;
typedef enum { NVS_READONLY = 0, NVS_READWRITE = 1 } nvs_open_mode_t;
typedef enum { NVS_TYPE_BLOB = 0x42 } nvs_type_t;
typedef struct nvs_iterator_s* nvs_iterator_t;
typedef struct {
  char namespace_name[16];
  char key[16];
  nvs_type_t type;
} nvs_entry_info_t;

esp_err_t nvs_open_from_partition(const char* partition, const char* ns, nvs_open_mode_t mode, nvs_handle_t* out);
esp_err_t nvs_set_blob(nvs_handle_t h, const char* key, const void* value, size_t len);
esp_err_t nvs_get_blob(nvs_handle_t h, const char* key, void* out, size_t* len);
esp_err_t nvs_erase_key(nvs_handle_t h, const char* key);
esp_err_t nvs_commit(nvs_handle_t h);
esp_err_t nvs_entry_find_in_handle(nvs_handle_t h, nvs_type_t type, nvs_iterator_t* it);
esp_err_t nvs_entry_next(nvs_iterator_t* it);
esp_err_t nvs_entry_info(nvs_iterator_t it, nvs_entry_info_t* info);
void nvs_release_iterator(nvs_iterator_t it);

void host_nvs_reset(void);            /* an empty flash */
uint32_t host_nvs_writes(void);       /* nvs_set_blob calls since the reset: what wears the flash */
uint32_t host_nvs_count(void);        /* records stored */
