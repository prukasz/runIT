#include "sys_actions.h"
#include "esp_rom_crc.h"
#include "nvs.h"
#include "router_ids.h"
#include "sdkconfig.h"
#include "sys_actions_router.h"
#include "sys_device.h"
#include "sys_system_router.h"
#include "utils.h"

#define NAMESPACE "actions"
#define KEY_LEN   10u                               // "a" + 8 hex digits + NUL
#define LEN_BYTES 2u                              // [u16 len] in front of every frame

ERR_MODULE_DEFINE(ERR_MOD_SYS_ACTIONS, "actions", ERR_SYS_ACTIONS_MAP)

/* The one record being written, on the heap while it is; NULL `image`: none. */
typedef struct {
  uint8_t* image;
  uint32_t id;
  uint16_t len;   // image bytes so far
  uint16_t want;  // the announced image length
} draft_t;

U_MUTEX_DEFINE(sys_actions_mutex);  // s_draft: both router tasks

static draft_t s_draft;
static nvs_handle_t s_nvs;
static __thread uint8_t s_depth;  // RUN nesting, per router task

/* ----------------------------------------------------------------------------
 * IDs, images, NVS
 * ------------------------------------------------------------------------- */

static err_h check_id(uint32_t id) {
  ERR_CHECK(id != SYS_ACTIONS_ID_NONE, ERR_ACTION_BAD_ID);
  return NULL;
}

static void key_of(uint32_t id, char key[KEY_LEN]) {
  snprintf(key, KEY_LEN, "a%08lx", (unsigned long)id);
}

// The ID of an NVS key written by key_of; 0 for any other key.
static uint32_t id_of(const char* key) {
  if (key[0] != 'a' || strlen(key) != KEY_LEN - 1) {
    return SYS_ACTIONS_ID_NONE;
  }
  uint32_t id = 0;
  for (uint32_t i = 1; i < KEY_LEN - 1; i++) {
    const char c = key[i];
    const uint32_t digit = (c >= '0' && c <= '9') ? (uint32_t)(c - '0') : (c >= 'a' && c <= 'f') ? (uint32_t)(c - 'a' + 10) : 16u;
    if (digit > 15) {
      return SYS_ACTIONS_ID_NONE;
    }
    id = (id << 4) | digit;
  }
  return id;
}

/* Walks an image's framing: the number of frames, or ERR_ACTION_BAD_FRAME at
 * the first frame that is empty, longer than a command or runs past the end. */
static err_h frames_check(const uint8_t* img, uint32_t len, uint16_t* count) {
  uint32_t pos = 0;
  uint16_t n = 0;
  while (pos < len) {
    uint16_t flen = 0;
    if (len - pos >= LEN_BYTES) {
      memcpy(&flen, img + pos, LEN_BYTES);
    }
    ERR_CHECK(flen >= 1 && flen <= CMD_DATA_MAX && flen <= len - pos - LEN_BYTES, ERR_ACTION_BAD_FRAME,
              .offset = (uint16_t)pos);
    pos += LEN_BYTES + flen;
    n++;
  }
  *count = n;
  return NULL;
}

static err_h store(uint32_t id, const uint8_t* image, uint32_t len) {
  char key[KEY_LEN];
  key_of(id, key);
  ERR_TRY_ESP(nvs_set_blob(s_nvs, key, image, len));
  return ERR_ESP(nvs_commit(s_nvs));
}

/* Reads a stored image into a new buffer with `extra` bytes after it (the
 * caller frees it). ERR_ACTION_NOT_FOUND when there is none. */
static err_h load(uint32_t id, uint32_t extra, uint8_t** out, size_t* len) {
  ERR_TRY(check_id(id));
  char key[KEY_LEN];
  key_of(id, key);
  esp_err_t rc = nvs_get_blob(s_nvs, key, NULL, len);
  ERR_CHECK(rc != ESP_ERR_NVS_NOT_FOUND, ERR_ACTION_NOT_FOUND, .id = id);
  ERR_TRY_ESP(rc);
  ERR_CHECK(*len > 0, ERR_ACTION_BAD_FRAME, .offset = 0);
  uint8_t* blob = malloc(*len + extra);
  ERR_CHECK(blob != NULL, ERR_ACTION_NO_MEM, .size = (uint32_t)(*len + extra));
  err_h err = ERR_ESP(nvs_get_blob(s_nvs, key, blob, len));
  if (err != NULL) {
    free(blob);
    return err;
  }
  *out = blob;
  return NULL;
}

// load() plus the framing check: the frame count of a usable image. Frees the blob on a failure.
static err_h load_checked(uint32_t id, uint32_t extra, uint8_t** blob, size_t* len, uint16_t* count) {
  ERR_TRY(load(id, extra, blob, len));
  err_h err = frames_check(*blob, *len, count);
  if (err != NULL) {
    free(*blob);
  }
  return err;
}

/* The onboard actions' images: [u16 len LE][keys + payload] per frame. A device-router write
 * [ROUTER_KEY_DEVICE][dir][SYS_DEVICE_ID_NONE u16][op u16] is the op for every device; a sys-router write
 * [ROUTER_KEY_SYS][dir][code] restarts the board. They match the sweep ops of sys_device and SYS_SYSTEM_OP_RESTART. */
#define U16(v)          (uint8_t)(v), (uint8_t)((v) >> 8)
#define SWEEP_FRAME(op) 6, 0, ROUTER_KEY_DEVICE, CMD_DIR_WRITE, U16(SYS_DEVICE_ID_NONE), U16(op)
#define SYS_FRAME(op)   3, 0, ROUTER_KEY_SYS, CMD_DIR_WRITE, (uint8_t)(op)

static const uint8_t k_resume_all[] = {SWEEP_FRAME(SYS_DEVICE_OP_RESUME)};
static const uint8_t k_suspend_all[] = {SWEEP_FRAME(SYS_DEVICE_OP_SUSPEND)};
static const uint8_t k_reset_all[] = {SWEEP_FRAME(SYS_DEVICE_OP_RESET)};
static const uint8_t k_hard_reset[] = {SYS_FRAME(SYS_SYSTEM_OP_RESTART)};

static const struct {
  uint32_t id;
  const uint8_t* image;
  uint16_t len;
} k_onboard[] = {
    {SYS_ACTIONS_ONBOARD_RESUME, k_resume_all, sizeof(k_resume_all)},
    {SYS_ACTIONS_ONBOARD_SUSPEND, k_suspend_all, sizeof(k_suspend_all)},
    {SYS_ACTIONS_ONBOARD_RESET, k_reset_all, sizeof(k_reset_all)},
    {SYS_ACTIONS_ONBOARD_HARD_RESET, k_hard_reset, sizeof(k_hard_reset)},
};

err_h sys_actions_init(void) {
  ERR_TRY(ERR_ESP(nvs_open_from_partition(CONFIG_SYS_ACTIONS_PARTITION, NAMESPACE, NVS_READWRITE, &s_nvs)));
  for (uint32_t i = 0; i < ARRAY_LEN(k_onboard); i++) {
    ERR_TRY(sys_actions_set(k_onboard[i].id, k_onboard[i].image, k_onboard[i].len));
  }
  return NULL;
}

/* ----------------------------------------------------------------------------
 * Run
 * ------------------------------------------------------------------------- */

// Routes every frame of a checked image; answers go to `scratch` (CMD_DATA_MAX) and are dropped. Stops at
// the first failure, or with `keep_going` runs them all, reports the later failures and returns the first.
static err_h replay(uint32_t id, const uint8_t* img, uint16_t count, uint8_t* scratch, bool keep_going) {
  err_h first = NULL;
  uint32_t pos = 0;
  for (uint16_t step = 0; step < count; step++) {
    uint16_t flen;
    memcpy(&flen, img + pos, LEN_BYTES);
    cmd_out_t out = {.data = scratch, .cap = CMD_DATA_MAX};
    err_h err = router_route((cmd_in_t){img + pos + LEN_BYTES, flen}, &out);
    pos += LEN_BYTES + flen;
    if (err == NULL) {
      continue;
    }
    err = ERR_WRAP(err, ERR_ACTION_STEP, .id = id, .step = step);
    if (!keep_going) {
      return err;
    }
    if (first == NULL) {
      first = err;
    } else {
      err_report(err);
    }
  }
  return first;
}

err_h sys_actions_run(uint32_t id, bool keep_going) {
  ERR_CHECK(router_origin() != NULL, ERR_ACTION_WRONG_TASK);
  ERR_CHECK(s_depth < CONFIG_SYS_ACTIONS_MAX_DEPTH, ERR_ACTION_DEPTH, .depth = s_depth);

  uint8_t* blob;
  size_t len;
  uint16_t count;
  ERR_TRY(load_checked(id, CMD_DATA_MAX, &blob, &len, &count));
  s_depth++;
  err_h err = replay(id, blob, count, blob + len, keep_going);
  s_depth--;
  free(blob);
  return err;
}

err_h sys_actions_request(uint32_t id, bool urgent) {
  ERR_TRY(check_id(id));
  uint8_t req[3 + sizeof(id)] = {ROUTER_KEY_ACTIONS, CMD_DIR_WRITE, SYS_ACTIONS_OP_RUN};
  memcpy(req + 3, &id, sizeof(id));  // LE on the ESP32
  return router_inject((cmd_in_t){req, sizeof(req)}, urgent ? ROUTER_FLAG_URGENT : 0, NULL, NULL);
}

/* ----------------------------------------------------------------------------
 * Writing a record
 * ------------------------------------------------------------------------- */

// Lock held.
static void draft_close(void) {
  free(s_draft.image);
  s_draft = (draft_t){0};
}

// Lock held. ERR_ACTION_BUSY unless none is open.
static err_h draft_open(uint32_t id, uint16_t len) {
  ERR_CHECK(s_draft.image == NULL, ERR_ACTION_BUSY);
  uint8_t* image = malloc(len);
  ERR_CHECK(image != NULL, ERR_ACTION_NO_MEM, .size = len);
  s_draft = (draft_t){.image = image, .id = id, .want = len};
  return NULL;
}

err_h sys_actions_record_begin(uint32_t id, uint16_t len) {
  ERR_TRY(check_id(id));
  ERR_CHECK(len > 0, ERR_ACTION_EMPTY);
  ERR_CHECK(len <= CONFIG_SYS_ACTIONS_MAX_BYTES, ERR_ACTION_TOO_BIG, .len = len, .max = CONFIG_SYS_ACTIONS_MAX_BYTES);
  (void)xSemaphoreTake(sys_actions_mutex, portMAX_DELAY);
  err_h err = draft_open(id, len);
  (void)xSemaphoreGive(sys_actions_mutex);
  return err;
}

// Lock held.
static err_h draft_write(uint16_t offset, const uint8_t* data, uint16_t len) {
  ERR_CHECK(s_draft.image != NULL, ERR_ACTION_NO_DRAFT);
  ERR_CHECK(offset == s_draft.len, ERR_ACTION_OFFSET, .got = offset, .want = s_draft.len);
  ERR_CHECK((uint32_t)offset + len <= s_draft.want, ERR_ACTION_TOO_BIG, .len = (uint16_t)(offset + len),
            .max = s_draft.want);
  memcpy(s_draft.image + offset, data, len);
  s_draft.len = (uint16_t)(offset + len);
  return NULL;
}

err_h sys_actions_record_write(uint16_t offset, const uint8_t* data, uint16_t len) {
  ERR_CHECK(data != NULL || len == 0, ERR_ACTION_BAD_ARG);
  (void)xSemaphoreTake(sys_actions_mutex, portMAX_DELAY);
  err_h err = draft_write(offset, data, len);
  (void)xSemaphoreGive(sys_actions_mutex);
  return err;
}

// Lock held, a draft open.
static err_h draft_store(uint32_t crc32) {
  ERR_CHECK(s_draft.len == s_draft.want, ERR_ACTION_INCOMPLETE, .got = s_draft.len, .want = s_draft.want);
  uint32_t crc = esp_rom_crc32_le(0, s_draft.image, s_draft.len);
  ERR_CHECK(crc == crc32, ERR_ACTION_CRC, .got = crc32, .want = crc);
  uint16_t count;
  ERR_TRY(frames_check(s_draft.image, s_draft.len, &count));
  return store(s_draft.id, s_draft.image, s_draft.len);
}

err_h sys_actions_record_commit(uint32_t crc32) {
  (void)xSemaphoreTake(sys_actions_mutex, portMAX_DELAY);
  err_h err;
  if (s_draft.image == NULL) {
    err = ERR_NEW(ERR_ACTION_NO_DRAFT);
  } else {
    err = draft_store(crc32);
    draft_close();
  }
  (void)xSemaphoreGive(sys_actions_mutex);
  return err;
}

void sys_actions_abort(void) {
  (void)xSemaphoreTake(sys_actions_mutex, portMAX_DELAY);
  draft_close();
  (void)xSemaphoreGive(sys_actions_mutex);
}

/* ----------------------------------------------------------------------------
 * Stored records
 * ------------------------------------------------------------------------- */

// True when `id` is stored with exactly this image.
static bool stored_equal(uint32_t id, const uint8_t* image, size_t size) {
  char key[KEY_LEN];
  key_of(id, key);
  size_t have = 0;
  if (nvs_get_blob(s_nvs, key, NULL, &have) != ESP_OK || have != size) {
    return false;
  }
  uint8_t* old = malloc(size);
  if (old == NULL) {
    return false;
  }
  const bool same = nvs_get_blob(s_nvs, key, old, &have) == ESP_OK && memcmp(old, image, size) == 0;
  free(old);
  return same;
}

err_h sys_actions_set(uint32_t id, const uint8_t* image, uint16_t len) {
  ERR_TRY(check_id(id));
  ERR_CHECK(image != NULL, ERR_ACTION_BAD_ARG);
  ERR_CHECK(len > 0, ERR_ACTION_EMPTY);
  ERR_CHECK(len <= CONFIG_SYS_ACTIONS_MAX_BYTES, ERR_ACTION_TOO_BIG, .len = len, .max = CONFIG_SYS_ACTIONS_MAX_BYTES);
  uint16_t count;
  ERR_TRY(frames_check(image, len, &count));
  return stored_equal(id, image, len) ? NULL : store(id, image, len);  // no flash write when it is already there
}

err_h sys_actions_copy(uint32_t from, uint32_t to) {
  uint8_t* blob;
  size_t len;
  ERR_TRY(load(from, 0, &blob, &len));
  err_h err = sys_actions_set(to, blob, (uint16_t)len);
  free(blob);
  return err;
}

err_h sys_actions_info(uint32_t id, sys_actions_info_t* out) {
  ERR_CHECK(out != NULL, ERR_ACTION_BAD_ARG);
  uint8_t* blob;
  size_t len;
  uint16_t count;
  ERR_TRY(load_checked(id, 0, &blob, &len, &count));
  out->bytes = (uint16_t)len;
  out->count = count;
  out->crc32 = esp_rom_crc32_le(0, blob, len);
  free(blob);
  return NULL;
}

err_h sys_actions_read(uint32_t id, uint16_t offset, uint8_t* buf, uint32_t cap, uint32_t* len) {
  ERR_CHECK(buf != NULL && len != NULL, ERR_ACTION_BAD_ARG);
  uint8_t* blob;
  size_t size;
  ERR_TRY(load(id, 0, &blob, &size));
  err_h err = NULL;
  if (offset > size) {
    err = ERR_NEW(ERR_ACTION_OFFSET, .got = offset, .want = (uint16_t)size);
  } else {
    *len = size - offset < cap ? (uint32_t)(size - offset) : cap;
    memcpy(buf, blob + offset, *len);
  }
  free(blob);
  return err;
}

err_h sys_actions_dump(uint32_t id, sys_actions_frame_f fn, void* ctx) {
  ERR_CHECK(fn != NULL, ERR_ACTION_BAD_ARG);
  uint8_t* blob;
  size_t len;
  uint16_t count;
  ERR_TRY(load_checked(id, 0, &blob, &len, &count));
  err_h err = NULL;
  const uint8_t* img = blob;
  for (uint16_t i = 0; i < count && err == NULL; i++) {
    uint16_t flen;
    memcpy(&flen, img, LEN_BYTES);
    err = fn(img + LEN_BYTES, flen, i + 1 == count, ctx);
    img += LEN_BYTES + flen;
  }
  free(blob);
  return err;
}

err_h sys_actions_remove(uint32_t id) {
  ERR_TRY(check_id(id));
  char key[KEY_LEN];
  key_of(id, key);
  esp_err_t rc = nvs_erase_key(s_nvs, key);
  ERR_CHECK(rc != ESP_ERR_NVS_NOT_FOUND, ERR_ACTION_NOT_FOUND, .id = id);
  ERR_TRY_ESP(rc);
  return ERR_ESP(nvs_commit(s_nvs));
}

err_h sys_actions_remove_all(void) {
  (void)xSemaphoreTake(sys_actions_mutex, portMAX_DELAY);
  err_h err = s_draft.image == NULL ? NULL : ERR_NEW(ERR_ACTION_BUSY);
  uint32_t ids[8];
  // Erasing invalidates the NVS iterator, so each pass lists afresh from the lowest ID.
  while (err == NULL) {
    uint16_t count = 0;
    err = sys_actions_list(0, ids, ARRAY_LEN(ids), &count, NULL);
    if (err != NULL || count == 0) {
      break;
    }
    for (uint16_t i = 0; i < count && err == NULL; i++) {
      err = sys_actions_remove(ids[i]);
    }
  }
  (void)xSemaphoreGive(sys_actions_mutex);
  return err;
}

// Keeps the `cap` smallest IDs seen so far, ascending.
static void keep_smallest(uint32_t* ids, uint16_t cap, uint16_t* count, uint32_t id) {
  uint16_t at;
  if (*count < cap) {
    at = (*count)++;
  } else if (id < ids[cap - 1]) {
    at = (uint16_t)(cap - 1);
  } else {
    return;
  }
  ids[at] = id;
  while (at > 0 && ids[at - 1] > ids[at]) {
    const uint32_t t = ids[at];
    ids[at] = ids[at - 1];
    ids[at - 1] = t;
    at--;
  }
}

err_h sys_actions_list(uint32_t start, uint32_t* ids, uint16_t cap, uint16_t* count, uint16_t* total) {
  ERR_CHECK(ids != NULL && count != NULL && cap > 0, ERR_ACTION_BAD_ARG);
  *count = 0;
  uint16_t all = 0;
  nvs_iterator_t it = NULL;
  esp_err_t rc = nvs_entry_find_in_handle(s_nvs, NVS_TYPE_BLOB, &it);
  while (rc == ESP_OK) {
    nvs_entry_info_t info;
    rc = nvs_entry_info(it, &info);
    if (rc != ESP_OK) {
      break;
    }
    const uint32_t id = id_of(info.key);
    if (id != SYS_ACTIONS_ID_NONE) {
      all++;
      if (id >= start) {
        keep_smallest(ids, cap, count, id);
      }
    }
    rc = nvs_entry_next(&it);
  }
  nvs_release_iterator(it);
  if (total != NULL) {
    *total = all;
  }
  return rc == ESP_ERR_NVS_NOT_FOUND ? NULL : ERR_ESP(rc);
}
