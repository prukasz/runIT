/* Reusable in-memory nvs_store_impl + software CRC-32 (IEEE 0xEDB88320) for host tests. */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "crc.h"
#include "nvs_store_iface.h"

uint32_t crc32_ieee(uint32_t crc, const void* data, uint32_t len) {
  const uint8_t* p = data;
  uint32_t c = ~crc;
  for (uint32_t i = 0; i < len; i++) {
    c ^= p[i];
    for (int b = 0; b < 8; b++) {
      c = (c & 1u) ? ((c >> 1) ^ 0xEDB88320u) : (c >> 1);
    }
  }
  return ~c;
}

#define MAX_NS    8
#define MAX_ITEMS 64
#define MAX_VAL   512

typedef struct {
  bool used;
  uint32_t ns;
  char key[16];
  uint8_t val[MAX_VAL];
  uint32_t len;
} item_t;

typedef struct {
  bool used;
  char part[16];
  char name[16];
} ns_rec_t;

static ns_rec_t s_ns[MAX_NS];
static item_t s_items[MAX_ITEMS];

void host_nvs_store_reset(void) {
  memset(s_ns, 0, sizeof(s_ns));
  memset(s_items, 0, sizeof(s_items));
}

static err_h impl_init(const char* partition) {
  (void)partition;
  return NULL;
}

static err_h impl_open(const char* partition, const char* name, nvs_store_ns_t* out) {
  ERR_CHECK(partition != NULL && name != NULL && out != NULL, ERR_NVS_BAD_ARG);
  for (uint32_t i = 0; i < MAX_NS; i++) {
    if (s_ns[i].used && strcmp(s_ns[i].part, partition) == 0 && strcmp(s_ns[i].name, name) == 0) {
      *out = (nvs_store_ns_t)(i + 1u);
      return NULL;
    }
  }
  for (uint32_t i = 0; i < MAX_NS; i++) {
    if (!s_ns[i].used) {
      s_ns[i].used = true;
      strncpy(s_ns[i].part, partition, sizeof(s_ns[i].part) - 1u);
      strncpy(s_ns[i].name, name, sizeof(s_ns[i].name) - 1u);
      *out = (nvs_store_ns_t)(i + 1u);
      return NULL;
    }
  }
  return ERR_NEW(ERR_NVS_BAD_ARG);
}

static err_h impl_get(nvs_store_ns_t ns, const char* key, void* buf, uint32_t* len) {
  ERR_CHECK(key != NULL && len != NULL, ERR_NVS_BAD_ARG);
  for (uint32_t i = 0; i < MAX_ITEMS; i++) {
    if (s_items[i].used && s_items[i].ns == ns && strcmp(s_items[i].key, key) == 0) {
      if (buf == NULL) {
        *len = s_items[i].len;
        return NULL;
      }
      ERR_CHECK(*len >= s_items[i].len, ERR_NVS_BAD_ARG);
      memcpy(buf, s_items[i].val, s_items[i].len);
      *len = s_items[i].len;
      return NULL;
    }
  }
  return ERR_NEW(ERR_NVS_NOT_FOUND);
}

static err_h impl_set(nvs_store_ns_t ns, const char* key, const void* data, uint32_t len) {
  ERR_CHECK(key != NULL && data != NULL && len <= MAX_VAL, ERR_NVS_BAD_ARG);
  int slot = -1;
  for (int i = 0; i < MAX_ITEMS; i++) {
    if (s_items[i].used && s_items[i].ns == ns && strcmp(s_items[i].key, key) == 0) {
      slot = i;
      break;
    }
    if (!s_items[i].used && slot < 0) {
      slot = i;
    }
  }
  ERR_CHECK(slot >= 0, ERR_NVS_BAD_ARG);
  s_items[slot].used = true;
  s_items[slot].ns = ns;
  strncpy(s_items[slot].key, key, sizeof(s_items[slot].key) - 1u);
  s_items[slot].key[sizeof(s_items[slot].key) - 1u] = '\0';
  memcpy(s_items[slot].val, data, len);
  s_items[slot].len = len;
  return NULL;
}

static err_h impl_erase(nvs_store_ns_t ns, const char* key) {
  if (key == NULL) {
    for (int i = 0; i < MAX_ITEMS; i++) {
      if (s_items[i].used && s_items[i].ns == ns) {
        s_items[i].used = false;
      }
    }
    return NULL;
  }
  for (int i = 0; i < MAX_ITEMS; i++) {
    if (s_items[i].used && s_items[i].ns == ns && strcmp(s_items[i].key, key) == 0) {
      s_items[i].used = false;
      return NULL;
    }
  }
  return ERR_NEW(ERR_NVS_NOT_FOUND);
}

static err_h impl_keys(nvs_store_ns_t ns, nvs_store_key_f fn, void* ctx) {
  for (int i = 0; i < MAX_ITEMS; i++) {
    if (s_items[i].used && s_items[i].ns == ns) {
      fn(s_items[i].key, ctx);
    }
  }
  return NULL;
}

const nvs_store_iface_t nvs_store_impl = {
    .init = impl_init,
    .open = impl_open,
    .get = impl_get,
    .set = impl_set,
    .erase = impl_erase,
    .keys = impl_keys,
};
