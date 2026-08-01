#include "sys_actions.h"
#include <esp_log.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dec_sys_actions.h"
#include "sys_device.h"
#include "sys_interface.h"
#include "utils.h"

// dec_sys_actions.h leaves OWNER set to OWNER_DEC_SYS_ACTIONS; take it back so
// this file's own SE_* macros are tagged as sys_actions, not as the decoder.
#undef OWNER
#define OWNER OWNER_SYS_ACTIONS_BASE

static const char* TAG = __FILE_NAME__;

#define SYS_ACTIONS_NVS_NAMESPACE "sys_actions"
#define SYS_ACTIONS_BOOT_ACTION_ID 0

static nvs_handle_t s_nvs = 0;

static void action_nvs_key(uint8_t action_id, char* out, size_t out_size) {
  snprintf(out, out_size, "act_%u", action_id);
}

// blob to store in RAM - runtime buffer only
typedef struct sys_action_t {
  size_t blob_size;
  uint8_t blob[];
} sys_action_t;

static action_static_func_t s_static_funcs[CONFIG_SYS_ACTIONS_STATIC_SLOTS];
static void* s_static_func_args[CONFIG_SYS_ACTIONS_STATIC_SLOTS];

// Guards the single active recording (s_recording/s_recording_id/s_has_recording),
// shared between the tap polling task (appending) and whatever task calls
R_MUTEX_DEFINE(s_actions_mutex);

static sys_action_t* s_recording = NULL;
static uint8_t s_recording_id = 0;
static bool s_has_recording = false;

// ---------------------------------------------------------
// NVS helpers - a single nvs_handle_t is safe to share across tasks (the NVS
// library serializes internally), so these need no mutex of their own.
// ---------------------------------------------------------

// Always returns a valid malloc'd sys_action_t* (blob_size == 0, *out_found ==
// false if nothing is stored under action_id yet) - callers that want to grow
// from empty (append_packet, record_start) don't need a separate branch.
static err_h nvs_load_action(uint8_t action_id, sys_action_t** out, bool* out_found) {
  char key[16];
  action_nvs_key(action_id, key, sizeof(key));

  size_t needed = 0;
  esp_err_t rc = nvs_get_blob(s_nvs, key, NULL, &needed);
  if (rc == ESP_ERR_NVS_NOT_FOUND) {
    sys_action_t* a = malloc(sizeof(sys_action_t));
    SE_CHECK_IF_ALLOCATED(a);
    a->blob_size = 0;
    *out = a;
    *out_found = false;
    return NULL;
  }
  if (rc != ESP_OK) {
    SE_RET_ERR(ERR_ESP_ERR, .esp_code = rc);
  }

  sys_action_t* a = malloc(sizeof(sys_action_t) + needed);
  SE_CHECK_IF_ALLOCATED(a);
  a->blob_size = needed;
  if (needed > 0) {
    rc = nvs_get_blob(s_nvs, key, a->blob, &needed);
    if (rc != ESP_OK) {
      free(a);
      SE_RET_ERR(ERR_ESP_ERR, .esp_code = rc);
    }
  }
  *out = a;
  *out_found = true;
  return NULL;
}

static err_h nvs_save_action(uint8_t action_id, const sys_action_t* a) {
  char key[16];
  action_nvs_key(action_id, key, sizeof(key));
  SE_RET_IF_ESP_ERR(nvs_set_blob(s_nvs, key, a->blob, a->blob_size));
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

// Reallocates *io_action to fit one more [u16 len][frame] record and appends
// it. On failure *io_action is left untouched (standard realloc semantics) -
// the caller's existing data is never lost by a failed grow.
static err_h grow_and_append(sys_action_t** io_action, const uint8_t* frame, size_t len) {
  sys_action_t* a = *io_action;
  size_t old_size = a->blob_size;
  size_t need = sizeof(uint16_t) + len;

  sys_action_t* bigger = realloc(a, sizeof(sys_action_t) + old_size + need);
  SE_CHECK_IF_ALLOCATED(bigger);

  uint16_t len16 = (uint16_t)len;
  memcpy(&bigger->blob[old_size], &len16, sizeof(len16));
  memcpy(&bigger->blob[old_size + sizeof(len16)], frame, len);
  bigger->blob_size = old_size + need;

  *io_action = bigger;
  return NULL;
}

// ---------------------------------------------------------
// Recording tap - drains sys_interface's tap buffer on its own schedule,
// instead of running inline inside the RX receiver task. See SYS_INTERFACE.MD.
// ---------------------------------------------------------

#include <sdkconfig.h>

R_TASK_DEFINE(s_actions_tap_task_handle, CONFIG_SYS_ACTIONS_TAP_TASK_STACK);

static void sys_actions_on_frame(const uint8_t* frame, size_t len) {
  // Never record our own control packets - otherwise a live "stop recording"
  // command would get captured into the action it was meant to stop.
  if (len == 0 || frame[0] == SYS_ACTIONS_CLASS_HEADER) return;

  R_MUTEX_LOCK(s_actions_mutex, WAIT_FOREVER);
  if (s_has_recording) {
    // Best-effort: a failed grow just stops the recording from growing
    // further rather than erroring out of a hook nothing checks.
    (void)grow_and_append(&s_recording, frame, len);
  }
  R_MUTEX_UNLOCK(s_actions_mutex);
}

static void sys_actions_tap_task(void* arg) {
  (void)arg;
  uint8_t frame[CONFIG_SYS_INTERFACE_RX_FRAME_CAP];

  while (1) {
    size_t len = 0;
    err_h err = sys_interface_tap_poll(frame, sizeof(frame), &len);
    if (SE_IS_ERR(err)) {
      SE_ORIGIN_CALL(err);
      continue;
    }
    if (len == 0) {
      vTaskDelay(pdMS_TO_TICKS(CONFIG_SYS_ACTIONS_TAP_POLL_MS));
      continue;
    }
    sys_actions_on_frame(frame, len);
  }
}

// ---------------------------------------------------------
// Static (hardcoded) actions - moved from the removed sys_states component.
// These are what used to be a state's "base action"; now just action ids
// 1-5's bound static function, registered at boot below.
// ---------------------------------------------------------

static err_h static_fn_freeze(void* arg) {
  (void)arg;
  return sys_device_freeze_all();
}

static err_h static_fn_resume(void* arg) {
  (void)arg;
  // Freeze and suspend are orthogonal in sys_device: only sync_all() clears a
  // freeze, only resume_all() clears a suspend. See SYS_DEVICE.MD.
  SE_RET_IF_ERR(sys_device_resume_all());
  SE_RET_IF_ERR(sys_device_sync_all());
  return NULL;
}

static err_h static_fn_suspend(void* arg) {
  (void)arg;
  return sys_device_suspend_all();
}

static err_h static_fn_reset(void* arg) {
  (void)arg;
  return sys_device_reset_all();
}

static err_h static_fn_hard_reset(void* arg) {
  (void)arg;
  return sys_device_uninstall_all();
}

// Boot-only, same convention as sys_interface_register_class() - not
// mutex-protected, register everything before any concurrent access starts.
static void register_default_static_actions(void) {
  sys_actions_bind_static(1, static_fn_freeze, NULL);
  sys_actions_bind_static(2, static_fn_resume, NULL);
  sys_actions_bind_static(3, static_fn_suspend, NULL);
  sys_actions_bind_static(4, static_fn_reset, NULL);
  sys_actions_bind_static(5, static_fn_hard_reset, NULL);
}

// ---------------------------------------------------------
// Public API
// ---------------------------------------------------------

err_h sys_actions_init(void) {
  SE_RET_IF_ESP_ERR(nvs_flash_init());
  SE_RET_IF_ESP_ERR(nvs_open(SYS_ACTIONS_NVS_NAMESPACE, NVS_READWRITE, &s_nvs));

  SE_RET_IF_ERR(sys_interface_register_class(SYS_ACTIONS_CLASS_HEADER, dec_sys_actions_decode, "sys_actions"));
  SE_RET_IF_ERR(sys_interface_tap_enable(CONFIG_SYS_ACTIONS_TAP_BUFFER_SIZE));
  if (s_actions_tap_task_handle == NULL) {
    R_TASK_START(s_actions_tap_task_handle, sys_actions_tap_task, NULL, CONFIG_SYS_ACTIONS_TAP_TASK_PRIO);
    if (s_actions_tap_task_handle == NULL) {
      SE_RET_ERR(ERR_BASE_NO_MEM, 0);
    }
  }

  register_default_static_actions();

  // Boot action: action 0 is always invoked here (static func 0, if ever
  // bound, plus whatever's stored in NVS). Nothing bound/stored yet is the
  // normal first-boot state, not a failure.
  err_h boot_err = sys_actions_invoke(SYS_ACTIONS_BOOT_ACTION_ID);
  if (SE_IS_ERR(boot_err) && boot_err->tag != ERR_ACTION_NOT_FOUND) {
    return boot_err;
  }

  ESP_LOGI(TAG, "sys_actions initialized (class 0x%02X)", SYS_ACTIONS_CLASS_HEADER);
  return NULL;
}

err_h sys_actions_bind_static(uint8_t action_id, action_static_func_t fn, void* arg) {
  SE_CHECK_IN_RANGE(action_id, 0, CONFIG_SYS_ACTIONS_STATIC_SLOTS - 1);
  s_static_funcs[action_id] = fn;
  s_static_func_args[action_id] = arg;
  return NULL;
}

err_h sys_actions_remove(uint8_t action_id) {
  SE_CHECK_IN_RANGE(action_id, 0, CONFIG_SYS_ACTIONS_ID_SPACE - 1);

  char key[16];
  action_nvs_key(action_id, key, sizeof(key));
  SE_RET_IF_ERR(nvs_erase(key));
  ESP_LOGI(TAG, "removed action %u", action_id);
  return NULL;
}

err_h sys_actions_remove_all(void) {
  // One find/erase/release cycle per key: erasing a key can invalidate an
  // in-progress iterator, so each erase restarts iteration from the top.
  // Action count is small (<= SYS_ACTIONS_ID_SPACE), so restarting is cheap.
  while (1) {
    nvs_iterator_t it = NULL;
    esp_err_t rc = nvs_entry_find_in_handle(s_nvs, NVS_TYPE_BLOB, &it);
    char key[NVS_KEY_NAME_MAX_SIZE] = {0};
    bool have_target = false;

    if (rc == ESP_OK) {
      nvs_entry_info_t info;
      nvs_entry_info(it, &info);
      strncpy(key, info.key, sizeof(key) - 1);
      have_target = true;
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
  SE_CHECK_IN_RANGE(action_id, 0, CONFIG_SYS_ACTIONS_ID_SPACE - 1);
  SE_CHECK_NOT_NULL(frame);
  SE_CHECK_IN_RANGE(len, 1, UINT16_MAX);

  sys_action_t* a = NULL;
  bool found = false;
  SE_RET_IF_ERR(nvs_load_action(action_id, &a, &found));

  err_h err = grow_and_append(&a, frame, len);
  if (SE_IS_OK(err)) {
    err = nvs_save_action(action_id, a);
  }
  free(a);

  SE_RET_IF_ERR(err);
  return NULL;
}

err_h sys_actions_record_start(uint8_t action_id) {
  SE_CHECK_IN_RANGE(action_id, 0, CONFIG_SYS_ACTIONS_ID_SPACE - 1);

  R_MUTEX_LOCK(s_actions_mutex, WAIT_FOREVER);
  bool busy = s_has_recording;
  R_MUTEX_UNLOCK(s_actions_mutex);
  if (busy) {
    SE_RET_ERR(ERR_ACTION_RECORDING_BUSY, action_id);
  }

  sys_action_t* a = NULL;
  bool found = false;
  SE_RET_IF_ERR(nvs_load_action(action_id, &a, &found));

  R_MUTEX_LOCK(s_actions_mutex, WAIT_FOREVER);
  if (s_has_recording) {
    R_MUTEX_UNLOCK(s_actions_mutex);
    free(a);
    SE_RET_ERR(ERR_ACTION_RECORDING_BUSY, action_id);
  }
  s_recording = a;
  s_recording_id = action_id;
  s_has_recording = true;
  R_MUTEX_UNLOCK(s_actions_mutex);

  ESP_LOGI(TAG, "recording action %u", action_id);
  return NULL;
}

err_h sys_actions_record_stop(uint8_t action_id) {
  R_MUTEX_LOCK(s_actions_mutex, WAIT_FOREVER);
  bool match = s_has_recording && s_recording_id == action_id;
  sys_action_t* a = match ? s_recording : NULL;
  if (match) {
    s_recording = NULL;
    s_has_recording = false;
  }
  R_MUTEX_UNLOCK(s_actions_mutex);

  ESP_LOGI(TAG, "stopped recording action %u", action_id);
  if (!match) return NULL;

  err_h err = nvs_save_action(action_id, a);
  free(a);
  SE_RET_IF_ERR(err);
  return NULL;
}

err_h sys_actions_invoke(uint8_t action_id) {
  SE_CHECK_IN_RANGE(action_id, 0, CONFIG_SYS_ACTIONS_ID_SPACE - 1);

  bool ran_static = false;
  if (action_id < CONFIG_SYS_ACTIONS_STATIC_SLOTS && s_static_funcs[action_id]) {
    ran_static = true;
    SE_RET_IF_ERR(s_static_funcs[action_id](s_static_func_args[action_id]));
  }

  sys_action_t* a = NULL;
  bool found = false;
  SE_RET_IF_ERR(nvs_load_action(action_id, &a, &found));

  if (!found) {
    free(a);
    if (!ran_static) {
      SE_RET_ERR(ERR_ACTION_NOT_FOUND, action_id);
    }
    return NULL;
  }

  ESP_LOGI(TAG, "invoking action %u (%u bytes)", action_id, (unsigned)a->blob_size);
  sys_interface_suspend_rx();

  err_h err = NULL;
  size_t off = 0;
  while (off + sizeof(uint16_t) <= a->blob_size) {
    uint16_t flen;
    memcpy(&flen, &a->blob[off], sizeof(flen));
    off += sizeof(flen);
    if (off + flen > a->blob_size) break; /* corrupt/truncated tail - stop */
    err = sys_interface_decode(&a->blob[off], flen);
    off += flen;
    if (SE_IS_ERR(err)) break;
  }

  sys_interface_resume_rx();
  free(a);

  SE_RET_IF_ERR(err);
  return NULL;
}
