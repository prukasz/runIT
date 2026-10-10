#include "sys_actions.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_rom_crc.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "sys_actions_router.h"
#include "utils.h"

#define TAG "sys_actions"

#define NAMESPACE "actions"
#define KEY_LEN 5u                                // "a007" + NUL
#define NAME_BYTES SYS_ACTIONS_NAME_MAX           // stored blob = [name, NUL-padded][image]
#define LEN_BYTES 2u                              // [u16 len] in front of every frame
#define FRAME_MAX (CONFIG_ROUTER_FRAME_MAX - 2u)  // keys + payload: a link frame without [seq][flags]

ERR_MODULE_DEFINE(ERR_MOD_SYS_ACTIONS, "actions", ERR_SYS_ACTIONS_MAP)

typedef enum {
  SESSION_NONE = 0,
  SESSION_RECORD,
  SESSION_UPLOAD,
} session_e;

/* The one open recording or upload; it fills s_blob (name first, then the image). */
typedef struct {
  session_e kind;
  uint8_t id;
  bool execute;    // record: frames run as usual (else taken without running)
  bool overflow;   // record: a frame didn't fit; `need` = the size it would have taken
  uint8_t link;    // record: the client being recorded
  uint32_t peer;
  uint16_t len;    // image bytes so far
  uint16_t want;   // upload: announced image length
  uint16_t count;  // record: frames
  uint32_t need;
} session_t;

U_MUTEX_DEFINE(sys_actions_mutex);  // s_session, s_blob: both router tasks record, ops open / close
#define LOCK() ((void)U_MUTEX_LOCK(sys_actions_mutex, WAIT_FOREVER))
#define UNLOCK() ((void)U_MUTEX_UNLOCK(sys_actions_mutex))

static session_t s_session;
static uint8_t s_blob[NAME_BYTES + CONFIG_SYS_ACTIONS_MAX_BYTES];
#define S_IMAGE (s_blob + NAME_BYTES)
static volatile bool s_recording;  // read by the tap without the lock (fast path for every frame)
static nvs_handle_t s_nvs;
static __thread uint8_t s_depth;   // RUN nesting, per router task

/* ----------------------------------------------------------------------------
 * IDs, names, images, NVS
 * ------------------------------------------------------------------------- */

static err_h check_name(const char* name) {
  ERR_CHECK(name != NULL, ERR_ACTION_BAD_ARG);
  size_t n = strnlen(name, NAME_BYTES);
  ERR_CHECK(n < NAME_BYTES, ERR_ACTION_BAD_NAME);
  for (size_t i = 0; i < n; i++) {
    ERR_CHECK((uint8_t)name[i] >= 0x20 && (uint8_t)name[i] <= 0x7E, ERR_ACTION_BAD_NAME);
  }
  return NULL;
}

// The stored name field: NUL-padded; `name` is already checked.
static void put_name(uint8_t* field, const char* name) {
  memset(field, 0, NAME_BYTES);
  memcpy(field, name, strlen(name));
}

static void key_of(uint8_t id, char key[KEY_LEN]) {
  snprintf(key, KEY_LEN, "a%03u", (unsigned)id);
}

// The ID of an NVS key written by key_of; 0 for any other key.
static uint8_t id_of(const char* key) {
  if (key[0] != 'a' || strlen(key) != KEY_LEN - 1) {
    return SYS_ACTIONS_ID_NONE;
  }
  uint32_t id = 0;
  for (uint32_t i = 1; i < KEY_LEN - 1; i++) {
    if (key[i] < '0' || key[i] > '9') {
      return SYS_ACTIONS_ID_NONE;
    }
    id = id * 10 + (uint32_t)(key[i] - '0');
  }
  return id <= UINT8_MAX ? (uint8_t)id : SYS_ACTIONS_ID_NONE;
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

static err_h store(uint8_t id, const uint8_t* blob, uint32_t len) {
  char key[KEY_LEN];
  key_of(id, key);
  ERR_TRY_ESP(nvs_set_blob(s_nvs, key, blob, len));
  return ERR_ESP(nvs_commit(s_nvs));
}

/* Reads a stored blob (name + image) into a new buffer with `extra` bytes
 * after it (the caller frees it). ERR_ACTION_NOT_FOUND when there is none. */
static err_h load(uint8_t id, uint32_t extra, uint8_t** out, size_t* len) {
  ERR_CHECK(id != SYS_ACTIONS_ID_NONE, ERR_ACTION_BAD_ID);
  char key[KEY_LEN];
  key_of(id, key);
  esp_err_t rc = nvs_get_blob(s_nvs, key, NULL, len);
  ERR_CHECK(rc != ESP_ERR_NVS_NOT_FOUND, ERR_ACTION_NOT_FOUND, .id = id);
  ERR_TRY_ESP(rc);
  ERR_CHECK(*len >= NAME_BYTES, ERR_ACTION_BAD_FRAME, .offset = 0);
  uint8_t* blob = malloc(*len + extra);
  ERR_CHECK(blob != NULL, ERR_ACTION_NO_MEM, .size = (uint32_t)(*len + extra));
  err_h err = ERR_ESP(nvs_get_blob(s_nvs, key, blob, len));
  if (err != NULL) {
    free(blob);
    return err;
  }
  blob[NAME_BYTES - 1] = '\0';  // the name field is always a C string
  *out = blob;
  return NULL;
}

static void info_of(const uint8_t* blob, uint32_t img_len, uint16_t count, sys_actions_info_t* out) {
  out->bytes = (uint16_t)img_len;
  out->count = count;
  out->crc32 = esp_rom_crc32_le(0, blob + NAME_BYTES, img_len);
  memcpy(out->name, blob, NAME_BYTES);
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

// Routes every frame of a checked image; answers go to `scratch` (CONFIG_ROUTER_FRAME_MAX) and are dropped.
static err_h replay(uint8_t id, const uint8_t* img, uint16_t count, uint8_t* scratch) {
  uint32_t pos = 0;
  for (uint16_t step = 0; step < count; step++) {
    uint16_t flen;
    memcpy(&flen, img + pos, LEN_BYTES);
    uint32_t out_len = 0;
    err_h err = router_route(img + pos + LEN_BYTES, flen, scratch, CONFIG_ROUTER_FRAME_MAX, &out_len);
    if (err != NULL) {
      return ERR_WRAP(err, ERR_ACTION_STEP, .id = id, .step = step);
    }
    pos += LEN_BYTES + flen;
  }
  return NULL;
}

err_h sys_actions_run(uint8_t id) {
  ERR_CHECK(router_origin() != NULL, ERR_ACTION_WRONG_TASK);
  ERR_CHECK(s_depth < CONFIG_SYS_ACTIONS_MAX_DEPTH, ERR_ACTION_DEPTH, .depth = s_depth);

  uint8_t* blob;
  size_t len;
  ERR_TRY(load(id, CONFIG_ROUTER_FRAME_MAX, &blob, &len));
  uint16_t count;
  err_h err = frames_check(blob + NAME_BYTES, len - NAME_BYTES, &count);
  if (err == NULL) {
    s_depth++;
    err = replay(id, blob + NAME_BYTES, count, blob + len);
    s_depth--;
  }
  free(blob);
  return err;
}

err_h sys_actions_request(uint8_t id, bool urgent) {
  ERR_CHECK(id != SYS_ACTIONS_ID_NONE, ERR_ACTION_BAD_ID);
  const uint8_t req[] = {ROUTER_KEY_ACTIONS, SYS_ACTIONS_OP_RUN, id};
  return router_inject(req, sizeof(req), urgent);
}

/* ----------------------------------------------------------------------------
 * Recording (router tap)
 * ------------------------------------------------------------------------- */

// Frames a recording keeps: not the sys key, not the actions ops other than RUN.
static bool recordable(const router_cmd_t* cmd) {
  uint8_t key = cmd->req[0];  // the dispatcher routes only frames with a root key
  if (key == ROUTER_KEY_SYS) {
    return false;
  }
  if (key == ROUTER_KEY_ACTIONS) {
    return cmd->req_len >= 2 && cmd->req[1] == SYS_ACTIONS_OP_RUN;
  }
  return true;
}

// Lock held: the frame comes from the client being recorded.
static bool from_recorded_client(const router_cmd_t* cmd) {
  return s_session.kind == SESSION_RECORD && s_session.link == cmd->link && s_session.peer == cmd->peer;
}

// Lock held. A frame that doesn't fit ends the growth: stop then refuses to store.
static void record_append(const uint8_t* req, uint32_t len) {
  if (s_session.overflow) {
    return;
  }
  uint32_t need = (uint32_t)s_session.len + LEN_BYTES + len;
  if (need > CONFIG_SYS_ACTIONS_MAX_BYTES) {
    s_session.overflow = true;
    s_session.need = need;
    return;
  }
  uint16_t flen = (uint16_t)len;
  memcpy(S_IMAGE + s_session.len, &flen, LEN_BYTES);
  memcpy(S_IMAGE + s_session.len + LEN_BYTES, req, len);
  s_session.len = (uint16_t)need;
  s_session.count++;
}

// Recording without execute: the client's frames are kept and answered OK, not run.
static bool tap_take(const router_cmd_t* cmd) {
  if (!s_recording || !recordable(cmd)) {
    return false;
  }
  bool taken = false;
  LOCK();
  if (from_recorded_client(cmd) && !s_session.execute) {
    record_append(cmd->req, cmd->req_len);
    taken = true;
  }
  UNLOCK();
  return taken;
}

// Recording with execute: the client's frames that succeeded are kept.
static void tap_seen(const router_cmd_t* cmd, err_h result) {
  if (!s_recording || result != NULL || !recordable(cmd)) {
    return;
  }
  LOCK();
  if (from_recorded_client(cmd) && s_session.execute) {
    record_append(cmd->req, cmd->req_len);
  }
  UNLOCK();
}

const router_tap_t g_sys_actions_tap = {.take = tap_take, .seen = tap_seen};

// Lock held.
static void session_close(void) {
  s_recording = false;
  s_session = (session_t){0};
}

// Lock held. ERR_ACTION_BUSY unless no session is open; `id` and `name` already checked.
static err_h session_open(session_e kind, uint8_t id, const char* name) {
  ERR_CHECK(s_session.kind == SESSION_NONE, ERR_ACTION_BUSY);
  s_session = (session_t){.kind = kind, .id = id};
  put_name(s_blob, name);
  return NULL;
}

err_h sys_actions_record_start(uint8_t id, const char* name, bool execute, uint8_t link, uint32_t peer) {
  ERR_CHECK(id != SYS_ACTIONS_ID_NONE, ERR_ACTION_BAD_ID);
  ERR_TRY(check_name(name));
  LOCK();
  err_h err = session_open(SESSION_RECORD, id, name);
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
            .max = CONFIG_SYS_ACTIONS_MAX_BYTES);
  ERR_CHECK(s_session.count > 0, ERR_ACTION_EMPTY);
  ERR_TRY(store(s_session.id, s_blob, NAME_BYTES + s_session.len));
  if (out != NULL) {
    info_of(s_blob, s_session.len, s_session.count, out);
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

err_h sys_actions_upload_begin(uint8_t id, const char* name, uint16_t len) {
  ERR_CHECK(id != SYS_ACTIONS_ID_NONE, ERR_ACTION_BAD_ID);
  ERR_TRY(check_name(name));
  ERR_CHECK(len > 0, ERR_ACTION_EMPTY);
  ERR_CHECK(len <= CONFIG_SYS_ACTIONS_MAX_BYTES, ERR_ACTION_TOO_BIG, .len = len, .max = CONFIG_SYS_ACTIONS_MAX_BYTES);
  LOCK();
  err_h err = session_open(SESSION_UPLOAD, id, name);
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
  memcpy(S_IMAGE + offset, data, len);
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
  uint32_t crc = esp_rom_crc32_le(0, S_IMAGE, s_session.len);
  ERR_CHECK(crc == crc32, ERR_ACTION_CRC, .got = crc32, .want = crc);
  uint16_t count;
  ERR_TRY(frames_check(S_IMAGE, s_session.len, &count));
  return store(s_session.id, s_blob, NAME_BYTES + s_session.len);
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
 * Stored actions
 * ------------------------------------------------------------------------- */

err_h sys_actions_info(uint8_t id, sys_actions_info_t* out) {
  ERR_CHECK(out != NULL, ERR_ACTION_BAD_ARG);
  uint8_t* blob;
  size_t len;
  ERR_TRY(load(id, 0, &blob, &len));
  uint16_t count;
  err_h err = frames_check(blob + NAME_BYTES, len - NAME_BYTES, &count);
  if (err == NULL) {
    info_of(blob, len - NAME_BYTES, count, out);
  }
  free(blob);
  return err;
}

err_h sys_actions_read(uint8_t id, uint16_t offset, uint8_t* out, uint16_t len) {
  ERR_CHECK(out != NULL || len == 0, ERR_ACTION_BAD_ARG);
  uint8_t* blob;
  size_t size;
  ERR_TRY(load(id, 0, &blob, &size));
  size -= NAME_BYTES;
  err_h err = NULL;
  if ((uint32_t)offset + len > size) {
    err = ERR_NEW(ERR_ACTION_READ_RANGE, .offset = offset, .len = len, .size = (uint16_t)size);
  } else {
    memcpy(out, blob + NAME_BYTES + offset, len);
  }
  free(blob);
  return err;
}

err_h sys_actions_rename(uint8_t id, const char* name) {
  ERR_TRY(check_name(name));
  uint8_t* blob;
  size_t len;
  ERR_TRY(load(id, 0, &blob, &len));
  put_name(blob, name);
  err_h err = store(id, blob, len);
  free(blob);
  return err;
}

err_h sys_actions_remove(uint8_t id) {
  ERR_CHECK(id != SYS_ACTIONS_ID_NONE, ERR_ACTION_BAD_ID);
  char key[KEY_LEN];
  key_of(id, key);
  esp_err_t rc = nvs_erase_key(s_nvs, key);
  ERR_CHECK(rc != ESP_ERR_NVS_NOT_FOUND, ERR_ACTION_NOT_FOUND, .id = id);
  ERR_TRY_ESP(rc);
  return ERR_ESP(nvs_commit(s_nvs));
}

// Bit n set = action n is stored (keys only, no blob read).
static err_h stored_ids(uint32_t ids[8], uint16_t* total) {
  memset(ids, 0, 8 * sizeof(uint32_t));
  *total = 0;
  nvs_iterator_t it = NULL;
  esp_err_t rc = nvs_entry_find_in_handle(s_nvs, NVS_TYPE_BLOB, &it);
  while (rc == ESP_OK) {
    nvs_entry_info_t info;
    rc = nvs_entry_info(it, &info);
    if (rc != ESP_OK) {
      break;
    }
    uint8_t id = id_of(info.key);
    if (id != SYS_ACTIONS_ID_NONE) {
      ids[id / 32] |= 1u << (id % 32);
      (*total)++;
    }
    rc = nvs_entry_next(&it);
  }
  nvs_release_iterator(it);
  return rc == ESP_ERR_NVS_NOT_FOUND ? NULL : ERR_ESP(rc);
}

err_h sys_actions_list(uint8_t start, sys_actions_list_f fn, void* ctx, uint16_t* total) {
  ERR_CHECK(fn != NULL, ERR_ACTION_BAD_ARG);
  uint32_t ids[8];
  uint16_t n;
  ERR_TRY(stored_ids(ids, &n));
  if (total != NULL) {
    *total = n;
  }
  for (uint32_t id = start > 0 ? start : 1; id <= UINT8_MAX; id++) {
    if (!(ids[id / 32] & (1u << (id % 32)))) {
      continue;
    }
    uint8_t* blob;
    size_t len;
    ERR_TRY(load((uint8_t)id, 0, &blob, &len));
    bool more = fn((uint8_t)id, (const char*)blob, ctx);
    free(blob);
    if (!more) {
      break;
    }
  }
  return NULL;
}
