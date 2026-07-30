#include "sys_actions.h"
#include <esp_log.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dec_sys_actions.h"
#include "sys_interface.h"
#include "utils.h"

// dec_sys_actions.h leaves OWNER set to OWNER_DEC_SYS_ACTIONS; take it back so
// this file's own SE_* macros are tagged as sys_actions, not as the decoder.
#undef OWNER
#define OWNER OWNER_SYS_ACTIONS_BASE

static const char* TAG = __FILE_NAME__;

// A dedicated namespace keeps room for `sys_vm`(or similar) to get its own
// NVS namespace later without key collisions - see SYS_ACTIONS.MD.
#define SYS_ACTIONS_NVS_NAMESPACE "sys_actions"
#define SYS_ACTIONS_BIND_MAP_KEY "bindmap"
#define SYS_ACTIONS_BOOT_ACTION_ID 0

static nvs_handle_t s_nvs = 0;

static void action_nvs_key(uint8_t action_id, char* out, size_t out_size) {
  snprintf(out, out_size, "act_%u", action_id);
}

typedef struct sys_action_t {
  uint8_t action_id;
  bool recording;
  uint8_t* buf;     /* staging buffer: [u16 len][frame]... */
  size_t max_size;  /* capacity of buf */
  size_t used;      /* bytes currently occupied */
  struct sys_action_t* next;
} sys_action_t;

typedef struct {
  uint8_t action_ids[SYS_STATES_ID_SPACE][SYS_ACTIONS_MAX_BOUND_PER_STATE];
  uint8_t counts[SYS_STATES_ID_SPACE];
} sys_actions_bind_map_t;

// R_MUTEX_DEFINE constructs this before app_main() runs, per the runit skill's
// static-allocation convention. Guards s_actions and s_bind_map: both are
// shared between the sys_if_rx task (via the recording tap) and whatever task
// calls the public API (sys_states, a callback route, ...).
R_MUTEX_DEFINE(s_actions_mutex);

static sys_action_t* s_actions = NULL;
static sys_actions_bind_map_t s_bind_map;

// ---------------------------------------------------------
// NVS helpers - a single nvs_handle_t is safe to share across tasks (the NVS
// library serializes internally), so these need no mutex of their own.
// ---------------------------------------------------------

static err_h nvs_read_blob_alloc(const char* key, uint8_t** out_buf, size_t* out_len, bool* out_found) {
  size_t needed = 0;
  esp_err_t rc = nvs_get_blob(s_nvs, key, NULL, &needed);
  if (rc == ESP_ERR_NVS_NOT_FOUND) {
    *out_found = false;
    *out_buf = NULL;
    *out_len = 0;
    return NULL;
  }
  if (rc != ESP_OK) {
    SE_RET_ERR(ERR_ESP_ERR, .esp_code = rc);
  }

  uint8_t* buf = malloc(needed > 0 ? needed : 1);
  SE_CHECK_IF_ALLOCATED(buf);
  if (needed > 0) {
    rc = nvs_get_blob(s_nvs, key, buf, &needed);
    if (rc != ESP_OK) {
      free(buf);
      SE_RET_ERR(ERR_ESP_ERR, .esp_code = rc);
    }
  }
  *out_found = true;
  *out_buf = buf;
  *out_len = needed;
  return NULL;
}

static err_h nvs_write_blob(const char* key, const void* data, size_t len) {
  SE_RET_IF_ESP_ERR(nvs_set_blob(s_nvs, key, data, len));
  SE_RET_IF_ESP_ERR(nvs_commit(s_nvs));
  return NULL;
}

static err_h nvs_erase(const char* key) {
  esp_err_t rc = nvs_erase_key(s_nvs, key);
  if (rc != ESP_OK && rc != ESP_ERR_NVS_NOT_FOUND) {
    SE_RET_ERR(ERR_ESP_ERR, .esp_code = rc);
  }
  SE_RET_IF_ESP_ERR(nvs_commit(s_nvs));
  return NULL;
}

static err_h load_bind_map(void) {
  uint8_t* buf = NULL;
  size_t len = 0;
  bool found = false;
  SE_RET_IF_ERR(nvs_read_blob_alloc(SYS_ACTIONS_BIND_MAP_KEY, &buf, &len, &found));
  if (found && len == sizeof(s_bind_map)) {
    memcpy(&s_bind_map, buf, sizeof(s_bind_map));
  } else {
    memset(&s_bind_map, 0, sizeof(s_bind_map));
  }
  free(buf);
  return NULL;
}

static err_h save_bind_map(void) {
  return nvs_write_blob(SYS_ACTIONS_BIND_MAP_KEY, &s_bind_map, sizeof(s_bind_map));
}

// ---------------------------------------------------------
// Internal helpers - assume s_actions_mutex is already held.
// Public functions each take the lock once and never call each other, so none
// of these may take the lock themselves (recursive lock on a plain
// SemaphoreHandle_t mutex would deadlock the calling task).
// ---------------------------------------------------------

static sys_action_t* find_action_locked(uint8_t action_id) {
  sys_action_t* a;
  LL_FOREACH(s_actions, a) {
    if (a->action_id == action_id) return a;
  }
  return NULL;
}

static err_h create_action_locked(uint8_t action_id, size_t max_size, sys_action_t** out) {
  sys_action_t* a = calloc(1, sizeof(sys_action_t));
  SE_CHECK_IF_ALLOCATED(a);
  a->buf = malloc(max_size);
  if (a->buf == NULL) {
    free(a);
    SE_RET_ERR(ERR_BASE_NO_MEM, 0);
  }
  a->action_id = action_id;
  a->max_size = max_size;

  LL_APPEND(s_actions, a);
  ESP_LOGI(TAG, "created action %u (capacity %u bytes)", action_id, (unsigned)max_size);
  *out = a;
  return NULL;
}

static void remove_action_locked(sys_action_t* a) {
  free(a->buf);
  LL_DELETE(s_actions, a);
  ESP_LOGI(TAG, "removed action %u (in-RAM staging)", a->action_id);
  free(a);
}

static err_h ensure_capacity_locked(sys_action_t* a, size_t needed) {
  if (needed <= a->max_size) return NULL;
  uint8_t* bigger = realloc(a->buf, needed);
  SE_CHECK_IF_ALLOCATED(bigger);
  a->buf = bigger;
  a->max_size = needed;
  return NULL;
}

static err_h append_packet_locked(sys_action_t* a, const uint8_t* frame, size_t len) {
  size_t need = sizeof(uint16_t) + len;
  if (a->used + need > a->max_size) {
    SE_RET_ERR(ERR_ACTION_BLOCK_FULL, .action_id = a->action_id, .used = (uint32_t)a->used, .size = (uint32_t)a->max_size);
  }
  uint16_t len16 = (uint16_t)len;
  memcpy(&a->buf[a->used], &len16, sizeof(len16));
  a->used += sizeof(len16);
  memcpy(&a->buf[a->used], frame, len);
  a->used += len;
  return NULL;
}

// ---------------------------------------------------------
// Recording tap - drains sys_interface's tap buffer on its own schedule,
// instead of running inline inside the RX receiver task. See SYS_INTERFACE.MD.
// ---------------------------------------------------------

#define SYS_ACTIONS_TAP_BUFFER_SIZE 1024
#define SYS_ACTIONS_TAP_TASK_STACK 4096
#define SYS_ACTIONS_TAP_POLL_MS 20

R_TASK_DEFINE(s_actions_tap_task_handle, SYS_ACTIONS_TAP_TASK_STACK);

static void sys_actions_on_frame(const uint8_t* frame, size_t len) {
  // Never record our own control packets - otherwise a live "stop recording"
  // command would get captured into the action it was meant to stop.
  if (len == 0 || frame[0] == SYS_ACTIONS_CLASS_HEADER) return;

  R_MUTEX_LOCK(s_actions_mutex, WAIT_FOREVER);
  sys_action_t* a;
  LL_FOREACH(s_actions, a) {
    if (a->recording) {
      // Best-effort: a full action just stops growing rather than erroring
      // out of a hook nothing checks the return value of.
      (void)append_packet_locked(a, frame, len);
    }
  }
  R_MUTEX_UNLOCK(s_actions_mutex);
}

static void sys_actions_tap_task(void* arg) {
  (void)arg;
  uint8_t frame[SYS_INTERFACE_RX_FRAME_CAP];

  while (1) {
    size_t len = 0;
    err_h err = sys_interface_tap_poll(frame, sizeof(frame), &len);
    if (SE_IS_ERR(err)) {
      SE_ORIGIN_CALL(err);
      continue;
    }
    if (len == 0) {
      vTaskDelay(pdMS_TO_TICKS(SYS_ACTIONS_TAP_POLL_MS));
      continue;
    }
    sys_actions_on_frame(frame, len);
  }
}

// ---------------------------------------------------------
// Public API
// ---------------------------------------------------------

err_h sys_actions_init(void) {
  SE_RET_IF_ESP_ERR(nvs_flash_init());
  SE_RET_IF_ESP_ERR(nvs_open(SYS_ACTIONS_NVS_NAMESPACE, NVS_READWRITE, &s_nvs));

  SE_RET_IF_ERR(load_bind_map());

  SE_RET_IF_ERR(sys_interface_register_class(SYS_ACTIONS_CLASS_HEADER, dec_sys_actions_decode, "sys_actions"));
  SE_RET_IF_ERR(sys_interface_tap_enable(SYS_ACTIONS_TAP_BUFFER_SIZE));
  if (s_actions_tap_task_handle == NULL) {
    R_TASK_START(s_actions_tap_task_handle, sys_actions_tap_task, NULL, 4);
    if (s_actions_tap_task_handle == NULL) {
      SE_RET_ERR(ERR_BASE_NO_MEM, 0);
    }
  }

  // Boot action: action 0 is always loaded from NVS and invoked here, if
  // anything is stored under it. Nothing stored yet is the normal first-boot
  // state, not a failure.
  err_h boot_err = sys_actions_invoke(SYS_ACTIONS_BOOT_ACTION_ID);
  if (SE_IS_ERR(boot_err) && boot_err->tag != ERR_ACTION_NOT_FOUND) {
    return boot_err;
  }

  ESP_LOGI(TAG, "sys_actions initialized (class 0x%02X)", SYS_ACTIONS_CLASS_HEADER);
  return NULL;
}

err_h sys_actions_create(uint8_t action_id, size_t max_size) {
  if (max_size == 0) {
    SE_RET_ERR(ERR_INVALID_VAL_UI32, .val = 0, .min = 1, .max = UINT32_MAX);
  }

  R_MUTEX_LOCK(s_actions_mutex, WAIT_FOREVER);
  err_h err = NULL;
  if (find_action_locked(action_id) == NULL) {
    sys_action_t* unused;
    err = create_action_locked(action_id, max_size, &unused);
  }
  R_MUTEX_UNLOCK(s_actions_mutex);

  SE_RET_IF_ERR(err);
  return NULL;
}

err_h sys_actions_remove(uint8_t action_id) {
  R_MUTEX_LOCK(s_actions_mutex, WAIT_FOREVER);
  sys_action_t* a = find_action_locked(action_id);
  if (a != NULL) remove_action_locked(a);
  R_MUTEX_UNLOCK(s_actions_mutex);

  char key[16];
  action_nvs_key(action_id, key, sizeof(key));
  SE_RET_IF_ERR(nvs_erase(key));
  return NULL;
}

err_h sys_actions_remove_all(void) {
  R_MUTEX_LOCK(s_actions_mutex, WAIT_FOREVER);
  sys_action_t *a, *tmp;
  LL_FOREACH_SAFE(s_actions, a, tmp) {
    free(a->buf);
    free(a);
  }
  s_actions = NULL;
  R_MUTEX_UNLOCK(s_actions_mutex);

  // One find/erase/release cycle per key: erasing a key can invalidate an
  // in-progress iterator, so each erase restarts iteration from the top.
  // Action count is small (<=256), so restarting is cheap. "bindmap" is left
  // alone - a binding to a now-missing action id just fails with
  // ERR_ACTION_NOT_FOUND when invoked, see SYS_ACTIONS.MD.
  while (1) {
    nvs_iterator_t it = NULL;
    esp_err_t rc = nvs_entry_find_in_handle(s_nvs, NVS_TYPE_BLOB, &it);
    char key[NVS_KEY_NAME_MAX_SIZE] = {0};
    bool have_target = false;

    while (rc == ESP_OK) {
      nvs_entry_info_t info;
      nvs_entry_info(it, &info);
      if (strcmp(info.key, SYS_ACTIONS_BIND_MAP_KEY) != 0) {
        strncpy(key, info.key, sizeof(key) - 1);
        have_target = true;
        break;
      }
      rc = nvs_entry_next(&it);
    }
    nvs_release_iterator(it);

    if (rc != ESP_OK && rc != ESP_ERR_NVS_NOT_FOUND) {
      SE_RET_ERR(ERR_ESP_ERR, .esp_code = rc);
    }
    if (!have_target) break;

    SE_RET_IF_ERR(nvs_erase(key));
  }

  ESP_LOGI(TAG, "removed all actions");
  return NULL;
}

err_h sys_actions_append_packet(uint8_t action_id, const uint8_t* frame, size_t len) {
  SE_CHECK_NOT_NULL(frame);
  if (len == 0 || len > UINT16_MAX) {
    SE_RET_ERR(ERR_INVALID_VAL_UI32, .val = (uint32_t)len, .min = 1, .max = UINT16_MAX);
  }

  R_MUTEX_LOCK(s_actions_mutex, WAIT_FOREVER);
  sys_action_t* a = find_action_locked(action_id);
  err_h err = NULL;
  if (a == NULL) {
    err = create_action_locked(action_id, SYS_ACTIONS_DEFAULT_MAX_SIZE, &a);
  }
  if (SE_IS_OK(err)) {
    err = append_packet_locked(a, frame, len);
  }
  R_MUTEX_UNLOCK(s_actions_mutex);

  SE_RET_IF_ERR(err);
  return NULL;
}

err_h sys_actions_record_start(uint8_t action_id) {
  R_MUTEX_LOCK(s_actions_mutex, WAIT_FOREVER);
  sys_action_t* a = find_action_locked(action_id);
  err_h err = NULL;
  if (a == NULL) {
    err = create_action_locked(action_id, SYS_ACTIONS_DEFAULT_MAX_SIZE, &a);
  }
  if (SE_IS_OK(err)) a->recording = true;
  R_MUTEX_UNLOCK(s_actions_mutex);

  SE_RET_IF_ERR(err);
  ESP_LOGI(TAG, "recording action %u", action_id);
  return NULL;
}

err_h sys_actions_record_stop(uint8_t action_id) {
  R_MUTEX_LOCK(s_actions_mutex, WAIT_FOREVER);
  sys_action_t* a = find_action_locked(action_id);
  if (a != NULL) a->recording = false;
  R_MUTEX_UNLOCK(s_actions_mutex);

  ESP_LOGI(TAG, "stopped recording action %u", action_id);
  if (a == NULL) return NULL;

  SE_RET_IF_ERR(sys_actions_persist_save(action_id));
  return NULL;
}

err_h sys_actions_invoke(uint8_t action_id) {
  char key[16];
  action_nvs_key(action_id, key, sizeof(key));

  uint8_t* blob = NULL;
  size_t blob_len = 0;
  bool found = false;
  SE_RET_IF_ERR(nvs_read_blob_alloc(key, &blob, &blob_len, &found));
  if (!found) {
    SE_RET_ERR(ERR_ACTION_NOT_FOUND, action_id);
  }

  ESP_LOGI(TAG, "invoking action %u (%u bytes)", action_id, (unsigned)blob_len);
  sys_interface_suspend_rx();

  err_h err = NULL;
  size_t off = 0;
  while (off + sizeof(uint16_t) <= blob_len) {
    uint16_t flen;
    memcpy(&flen, &blob[off], sizeof(flen));
    off += sizeof(flen);
    if (off + flen > blob_len) break; /* corrupt/truncated tail - stop */
    err = sys_interface_decode(&blob[off], flen);
    off += flen;
    if (SE_IS_ERR(err)) break;
  }

  sys_interface_resume_rx();
  free(blob);

  SE_RET_IF_ERR(err);
  return NULL;
}

err_h sys_actions_bind_state(uint8_t action_id, sys_state_e state) {
  if (state >= SYS_STATES_ID_SPACE) {
    SE_RET_ERR(ERR_INVALID_VAL_UI32, .val = (uint32_t)state, .min = 0, .max = SYS_STATES_ID_SPACE - 1);
  }

  R_MUTEX_LOCK(s_actions_mutex, WAIT_FOREVER);
  uint8_t count = s_bind_map.counts[state];
  bool already = false;
  for (uint8_t i = 0; i < count; i++) {
    if (s_bind_map.action_ids[state][i] == action_id) {
      already = true;
      break;
    }
  }
  err_h err = NULL;
  if (!already) {
    if (count >= SYS_ACTIONS_MAX_BOUND_PER_STATE) {
      SE_SET_ERR(err, ERR_ACTION_STATE_BINDINGS_FULL, .action_id = action_id, .state = (uint8_t)state);
    } else {
      s_bind_map.action_ids[state][count] = action_id;
      s_bind_map.counts[state] = (uint8_t)(count + 1);
    }
  }
  R_MUTEX_UNLOCK(s_actions_mutex);

  SE_RET_IF_ERR(err);
  if (already) return NULL;

  SE_RET_IF_ERR(save_bind_map());
  ESP_LOGI(TAG, "bound action %u to state %u", action_id, (unsigned)state);
  return NULL;
}

err_h sys_actions_invoke_for_state(sys_state_e state) {
  if (state >= SYS_STATES_ID_SPACE) {
    SE_RET_ERR(ERR_INVALID_VAL_UI32, .val = (uint32_t)state, .min = 0, .max = SYS_STATES_ID_SPACE - 1);
  }

  R_MUTEX_LOCK(s_actions_mutex, WAIT_FOREVER);
  uint8_t count = s_bind_map.counts[state];
  uint8_t ids[SYS_ACTIONS_MAX_BOUND_PER_STATE];
  memcpy(ids, s_bind_map.action_ids[state], count);
  R_MUTEX_UNLOCK(s_actions_mutex);

  for (uint8_t i = 0; i < count; i++) {
    SE_RET_IF_ERR(sys_actions_invoke(ids[i]));
  }
  return NULL;
}

err_h sys_actions_persist_save(uint8_t action_id) {
  R_MUTEX_LOCK(s_actions_mutex, WAIT_FOREVER);
  sys_action_t* a = find_action_locked(action_id);
  err_h err = NULL;
  uint8_t* buf_copy = NULL;
  size_t used = 0;
  if (a == NULL) {
    SE_SET_ERR(err, ERR_ACTION_NOT_FOUND, action_id);
  } else {
    used = a->used;
    if (used > 0) {
      buf_copy = malloc(used);
      if (buf_copy == NULL) {
        SE_SET_ERR(err, ERR_BASE_NO_MEM, 0);
      } else {
        memcpy(buf_copy, a->buf, used);
      }
    }
  }
  R_MUTEX_UNLOCK(s_actions_mutex);

  SE_RET_IF_ERR(err);

  char key[16];
  action_nvs_key(action_id, key, sizeof(key));
  err = nvs_write_blob(key, buf_copy, used);
  free(buf_copy);
  SE_RET_IF_ERR(err);

  ESP_LOGI(TAG, "persisted action %u (%u bytes)", action_id, (unsigned)used);
  return NULL;
}

err_h sys_actions_persist_load(uint8_t action_id) {
  char key[16];
  action_nvs_key(action_id, key, sizeof(key));

  uint8_t* blob = NULL;
  size_t blob_len = 0;
  bool found = false;
  SE_RET_IF_ERR(nvs_read_blob_alloc(key, &blob, &blob_len, &found));
  if (!found) {
    SE_RET_ERR(ERR_ACTION_NOT_FOUND, action_id);
  }

  R_MUTEX_LOCK(s_actions_mutex, WAIT_FOREVER);
  sys_action_t* a = find_action_locked(action_id);
  err_h err = NULL;
  if (a == NULL) {
    err = create_action_locked(action_id, blob_len > 0 ? blob_len : SYS_ACTIONS_DEFAULT_MAX_SIZE, &a);
  }
  if (SE_IS_OK(err)) err = ensure_capacity_locked(a, blob_len);
  if (SE_IS_OK(err)) {
    if (blob_len > 0) memcpy(a->buf, blob, blob_len);
    a->used = blob_len;
  }
  R_MUTEX_UNLOCK(s_actions_mutex);
  free(blob);

  SE_RET_IF_ERR(err);
  ESP_LOGI(TAG, "loaded action %u (%u bytes) from NVS", action_id, (unsigned)blob_len);
  return NULL;
}
