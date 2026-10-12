#include "sys_actions.h"
#include "esp_log.h"
#include "esp_rom_crc.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "sys_actions_router.h"
#include "utils.h"

#define TAG __FILE_NAME__

#define NAMESPACE "actions"
#define KEY_LEN 10u                               // "a" + 8 hex digits + NUL
#define LEN_BYTES 2u                              // [u16 len] in front of every frame
#define FRAME_MAX (CONFIG_ROUTER_FRAME_MAX - 2u)  // keys + payload: a link frame without [seq][flags]

ERR_MODULE_DEFINE(ERR_MOD_SYS_ACTIONS, "actions", ERR_SYS_ACTIONS_MAP)

typedef enum {
  SESSION_NONE = 0,
  SESSION_RECORD,
  SESSION_UPLOAD,
} session_e;

/* The one open recording or upload; it fills s_image. */
typedef struct {
  session_e kind;
  uint32_t id;
  bool execute;    // record: frames run as usual (else taken without running)
  bool overflow;   // record: a frame didn't fit; `need` = the size it would have taken
  uint8_t link;    // record: the client being recorded
  uint32_t peer;
  uint16_t len;    // image bytes so far
  uint16_t want;   // upload: announced image length
  uint16_t count;  // record: frames
  uint32_t need;
} session_t;

U_MUTEX_DEFINE(sys_actions_mutex);  // s_session, s_image: both router tasks record, ops open / close
#define LOCK() ((void)U_MUTEX_LOCK(sys_actions_mutex, WAIT_FOREVER))
#define UNLOCK() ((void)U_MUTEX_UNLOCK(sys_actions_mutex))

static session_t s_session;
_Static_assert(CONFIG_SYS_ACTIONS_MAX_BYTES_DEVICE <= CONFIG_SYS_ACTIONS_MAX_BYTES,
               "the device record limit is above the buffer (SYS_ACTIONS_MAX_BYTES)");

static uint8_t s_image[CONFIG_SYS_ACTIONS_MAX_BYTES];
static volatile bool s_recording;  // read by the tap without the lock (fast path for every frame)
static nvs_handle_t s_nvs;
static __thread uint8_t s_depth;   // RUN nesting, per router task

/* ----------------------------------------------------------------------------
 * IDs, images, NVS
 * ------------------------------------------------------------------------- */

static err_h check_id(uint32_t id) {
  ERR_CHECK(id != SYS_ACTIONS_ID_NONE, ERR_ACTION_BAD_ID);
  return NULL;
}

// The largest image a record of this ID may have: its class decides.
static uint32_t max_bytes(uint32_t id) {
  return (id >> 24) == SYS_ACTIONS_CLASS_DEVICE ? CONFIG_SYS_ACTIONS_MAX_BYTES_DEVICE : CONFIG_SYS_ACTIONS_MAX_BYTES;
}

uint32_t sys_actions_max_bytes(uint32_t id) {
  return max_bytes(id);
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
 * the first frame that is empty, longer than a link frame or runs past the end. */
static err_h frames_check(const uint8_t* img, uint32_t len, uint16_t* count) {
  uint32_t pos = 0;
  uint16_t n = 0;
  while (pos < len) {
    uint16_t flen = 0;
    if (len - pos >= LEN_BYTES) {
      memcpy(&flen, img + pos, LEN_BYTES);
    }
    ERR_CHECK(flen >= 1 && flen <= FRAME_MAX && flen <= len - pos - LEN_BYTES, ERR_ACTION_BAD_FRAME,
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

static void info_of(const uint8_t* image, uint32_t len, uint16_t count, sys_actions_info_t* out) {
  out->bytes = (uint16_t)len;
  out->count = count;
  out->crc32 = esp_rom_crc32_le(0, image, len);
}

err_h sys_actions_init(void) {
  esp_err_t rc = nvs_flash_init_partition(CONFIG_SYS_ACTIONS_PARTITION);
  if (rc == ESP_ERR_NVS_NO_FREE_PAGES || rc == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_LOGW(TAG, "partition '%s' unusable (%s): erasing", CONFIG_SYS_ACTIONS_PARTITION, esp_err_to_name(rc));
    ERR_TRY_ESP(nvs_flash_erase_partition(CONFIG_SYS_ACTIONS_PARTITION));
    rc = nvs_flash_init_partition(CONFIG_SYS_ACTIONS_PARTITION);
  }
  ERR_TRY_ESP(rc);
  return ERR_ESP(nvs_open_from_partition(CONFIG_SYS_ACTIONS_PARTITION, NAMESPACE, NVS_READWRITE, &s_nvs));
}

/* ----------------------------------------------------------------------------
 * Run
 * ------------------------------------------------------------------------- */

// Routes every frame of a checked image; answers go to `scratch` (CONFIG_ROUTER_FRAME_MAX) and are dropped. Stops at
// the first failure, or with `keep_going` runs them all, reports the later failures and returns the first.
static err_h replay(uint32_t id, const uint8_t* img, uint16_t count, uint8_t* scratch, bool keep_going) {
  err_h first = NULL;
  uint32_t pos = 0;
  for (uint16_t step = 0; step < count; step++) {
    uint16_t flen;
    memcpy(&flen, img + pos, LEN_BYTES);
    uint32_t out_len = 0;
    err_h err = router_route(img + pos + LEN_BYTES, flen, scratch, CONFIG_ROUTER_FRAME_MAX, &out_len);
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
  ERR_TRY(load_checked(id, CONFIG_ROUTER_FRAME_MAX, &blob, &len, &count));
  s_depth++;
  err_h err = replay(id, blob, count, blob + len, keep_going);
  s_depth--;
  free(blob);
  return err;
}

err_h sys_actions_request(uint32_t id, bool urgent) {
  ERR_TRY(check_id(id));
  uint8_t req[2 + sizeof(id)] = {ROUTER_KEY_ACTIONS, SYS_ACTIONS_OP_RUN};
  memcpy(req + 2, &id, sizeof(id));  // LE on the ESP32
  return router_inject(req, sizeof(req), urgent);
}

/* ----------------------------------------------------------------------------
 * Recording (router tap)
 * ------------------------------------------------------------------------- */

// Frames a recording keeps: not the sys key, not the actions ops other than RUN / RUN_ALL.
static bool recordable(const router_cmd_t* cmd) {
  uint8_t key = cmd->req[0];  // the dispatcher routes only frames with a root key
  if (key == ROUTER_KEY_SYS) {
    return false;
  }
  if (key == ROUTER_KEY_ACTIONS) {
    return cmd->req_len >= 2 && (cmd->req[1] == SYS_ACTIONS_OP_RUN || cmd->req[1] == SYS_ACTIONS_OP_RUN_ALL);
  }
  return true;
}

// Lock held. A frame that doesn't fit ends the growth: stop then refuses to store.
static void record_append(const uint8_t* req, uint32_t len) {
  if (s_session.overflow) {
    return;
  }
  uint32_t need = (uint32_t)s_session.len + LEN_BYTES + len;
  if (need > max_bytes(s_session.id)) {
    s_session.overflow = true;
    s_session.need = need;
    return;
  }
  uint16_t flen = (uint16_t)len;
  memcpy(s_image + s_session.len, &flen, LEN_BYTES);
  memcpy(s_image + s_session.len + LEN_BYTES, req, len);
  s_session.len = (uint16_t)need;
  s_session.count++;
}

// Keeps the frame when it comes from the client being recorded and the recording is in mode `execute`.
static bool record_frame(const router_cmd_t* cmd, bool execute) {
  LOCK();
  const bool mine = s_session.kind == SESSION_RECORD && s_session.execute == execute &&
                    s_session.link == cmd->link && s_session.peer == cmd->peer;
  if (mine) {
    record_append(cmd->req, cmd->req_len);
  }
  UNLOCK();
  return mine;
}

// Recording without execute: the client's frames are kept and answered OK, not run.
static bool tap_take(const router_cmd_t* cmd) {
  return s_recording && recordable(cmd) && record_frame(cmd, false);
}

// Recording with execute: the client's frames that succeeded are kept.
static void tap_seen(const router_cmd_t* cmd, err_h result) {
  if (s_recording && result == NULL && recordable(cmd)) {
    (void)record_frame(cmd, true);
  }
}

const router_tap_t g_sys_actions_tap = {.take = tap_take, .seen = tap_seen};

// Lock held.
static void session_close(void) {
  s_recording = false;
  s_session = (session_t){0};
}

// Lock held. ERR_ACTION_BUSY unless no session is open; `id` already checked.
static err_h session_open(session_e kind, uint32_t id) {
  ERR_CHECK(s_session.kind == SESSION_NONE, ERR_ACTION_BUSY);
  s_session = (session_t){.kind = kind, .id = id};
  return NULL;
}

err_h sys_actions_record_start(uint32_t id, bool execute, uint8_t link, uint32_t peer) {
  ERR_TRY(check_id(id));
  LOCK();
  err_h err = session_open(SESSION_RECORD, id);
  if (err == NULL) {
    s_session.execute = execute;
    s_session.link = link;
    s_session.peer = peer;
    s_recording = true;
  }
  UNLOCK();
  return err;
}

// Lock held, a recording open.
static err_h record_store(sys_actions_info_t* out) {
  ERR_CHECK(!s_session.overflow, ERR_ACTION_TOO_BIG,
            .len = (uint16_t)(s_session.need > UINT16_MAX ? UINT16_MAX : s_session.need),
            .max = (uint16_t)max_bytes(s_session.id));
  ERR_CHECK(s_session.count > 0, ERR_ACTION_EMPTY);
  ERR_TRY(store(s_session.id, s_image, s_session.len));
  if (out != NULL) {
    info_of(s_image, s_session.len, s_session.count, out);
  }
  return NULL;
}

err_h sys_actions_record_stop(sys_actions_info_t* out) {
  LOCK();
  err_h err;
  if (s_session.kind != SESSION_RECORD) {
    err = ERR_NEW(ERR_ACTION_NO_SESSION);
  } else {
    err = record_store(out);
    session_close();
  }
  UNLOCK();
  return err;
}

/* ----------------------------------------------------------------------------
 * Upload
 * ------------------------------------------------------------------------- */

err_h sys_actions_upload_begin(uint32_t id, uint16_t len) {
  ERR_TRY(check_id(id));
  ERR_CHECK(len > 0, ERR_ACTION_EMPTY);
  ERR_CHECK(len <= max_bytes(id), ERR_ACTION_TOO_BIG, .len = len, .max = (uint16_t)max_bytes(id));
  LOCK();
  err_h err = session_open(SESSION_UPLOAD, id);
  if (err == NULL) {
    s_session.want = len;
  }
  UNLOCK();
  return err;
}

// Lock held.
static err_h upload_write(uint16_t offset, const uint8_t* data, uint16_t len) {
  ERR_CHECK(s_session.kind == SESSION_UPLOAD, ERR_ACTION_NO_SESSION);
  ERR_CHECK(offset == s_session.len, ERR_ACTION_OFFSET, .got = offset, .want = s_session.len);
  ERR_CHECK((uint32_t)offset + len <= s_session.want, ERR_ACTION_TOO_BIG, .len = (uint16_t)(offset + len),
            .max = s_session.want);
  memcpy(s_image + offset, data, len);
  s_session.len = (uint16_t)(offset + len);
  return NULL;
}

err_h sys_actions_upload_write(uint16_t offset, const uint8_t* data, uint16_t len) {
  ERR_CHECK(data != NULL || len == 0, ERR_ACTION_BAD_ARG);
  LOCK();
  err_h err = upload_write(offset, data, len);
  UNLOCK();
  return err;
}

// Lock held, an upload open.
static err_h upload_store(uint32_t crc32) {
  ERR_CHECK(s_session.len == s_session.want, ERR_ACTION_INCOMPLETE, .got = s_session.len, .want = s_session.want);
  uint32_t crc = esp_rom_crc32_le(0, s_image, s_session.len);
  ERR_CHECK(crc == crc32, ERR_ACTION_CRC, .got = crc32, .want = crc);
  uint16_t count;
  ERR_TRY(frames_check(s_image, s_session.len, &count));
  return store(s_session.id, s_image, s_session.len);
}

err_h sys_actions_upload_commit(uint32_t crc32) {
  LOCK();
  err_h err;
  if (s_session.kind != SESSION_UPLOAD) {
    err = ERR_NEW(ERR_ACTION_NO_SESSION);
  } else {
    err = upload_store(crc32);
    session_close();
  }
  UNLOCK();
  return err;
}

void sys_actions_abort(void) {
  LOCK();
  session_close();
  UNLOCK();
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
  ERR_CHECK(len <= max_bytes(id), ERR_ACTION_TOO_BIG, .len = len, .max = (uint16_t)max_bytes(id));
  uint16_t count;
  ERR_TRY(frames_check(image, len, &count));
  return stored_equal(id, image, len) ? NULL : store(id, image, len);  // no flash write when it is already there
}

err_h sys_actions_copy(uint32_t from, uint32_t to) {
  ERR_TRY(check_id(to));
  uint8_t* blob;
  size_t len;
  uint16_t count;
  ERR_TRY(load_checked(from, 0, &blob, &len, &count));
  err_h err = NULL;
  if (len > max_bytes(to)) {
    err = ERR_NEW(ERR_ACTION_TOO_BIG, .len = (uint16_t)len, .max = (uint16_t)max_bytes(to));
  } else if (!stored_equal(to, blob, len)) {
    err = store(to, blob, len);
  }
  free(blob);
  return err;
}

err_h sys_actions_info(uint32_t id, sys_actions_info_t* out) {
  ERR_CHECK(out != NULL, ERR_ACTION_BAD_ARG);
  uint8_t* blob;
  size_t len;
  uint16_t count;
  ERR_TRY(load_checked(id, 0, &blob, &len, &count));
  info_of(blob, len, count, out);
  free(blob);
  return NULL;
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
