#include "sys_actions.h"
#include <nvs.h>
#include <nvs_flash.h>
#include <sdkconfig.h>
#include "sys_actions_static.h"
#include "sys_interface.h"
#include "utils.h"

// This file's DBG() calls fire on CONFIG_DBG_GLOBAL or this component's own
// switch (components/utils/Kconfig) - see DBG()'s doc comment in utils.h.
#define DBG_ENABLE CONFIG_DBG_ENABLE_SYS_ACTIONS

#undef OWNER
#define OWNER OWNER_SYS_ACTIONS_BASE

static const char* TAG = __FILE_NAME__;
#define SYS_ACTIONS_NVS_NAMESPACE "sys_actions"

static nvs_handle_t s_nvs = 0;
R_TASK_DEFINE(s_actions_tap_task_handle, CONFIG_SYS_ACTIONS_TAP_TASK_STACK);
/**
 * @brief Guards the single active recording.
 *
 * Shared by the tap polling task and callers of record start/stop.
 */
R_MUTEX_DEFINE(s_actions_mutex);

/* Requests from callers that must not run an action on their own task (the VM
   supervisor: an action may rewind or unload the VM, and a replay outlasts the
   block watchdog). The tap task runs them between polls. */
typedef struct {
  uint8_t scope;
  uint8_t id;
} action_request_t;
R_QUEUE_DEFINE(s_actions_requests, CONFIG_SYS_ACTIONS_REQUEST_QUEUE_LEN, sizeof(action_request_t));

static void action_make_nvs_key(uint8_t action_id, char* out, size_t out_size) {
  snprintf(out, out_size, "act_%u", action_id);
}

/** @brief Runtime representation of one dynamically recorded action. */
typedef struct sys_action_t {
  size_t  blob_size;
  uint8_t blob[];
} sys_action_t;

static action_static_func_t s_static_funcs[CONFIG_SYS_ACTIONS_STATIC_SLOTS];

static sys_action_t* s_recording     = NULL;
static uint8_t       s_recording_id  = 0;
static bool          s_has_recording = false;

/**
 * @brief Load one dynamic action from NVS.
 *
 * NVS serializes access internally, so no actions mutex is required here.
 * When the key does not exist, this returns an allocated empty action and sets
 * @p out_found to false.
 *
 * @param action_id Dynamic action identifier.
 * @param out Receives the allocated action.
 * @param out_found Receives whether the NVS key existed.
 * @return NULL on success, otherwise an error handle.
 */
static SE_MUST_USE err_h nvs_load_action(uint8_t action_id, sys_action_t** out, bool* out_found) {
  char key[16];
  action_make_nvs_key(action_id, key, sizeof(key));

  size_t    needed = 0;
  esp_err_t rc     = nvs_get_blob(s_nvs, key, NULL, &needed);
  if (rc == ESP_ERR_NVS_NOT_FOUND) {
    sys_action_t* a = malloc(sizeof(sys_action_t));
    SE_CHECK_IF_ALLOCATED(a);
    a->blob_size = 0;
    *out         = a;
    *out_found   = false;
    return NULL;
  }
  if (rc != ESP_OK) {
    SE_FAIL(ERR_ESP_ERR, .esp_code = rc);
  }

  if (needed > CONFIG_SYS_ACTIONS_MAX_BLOB_SIZE) {
    needed = CONFIG_SYS_ACTIONS_MAX_BLOB_SIZE;
  }

  sys_action_t* a = malloc(sizeof(sys_action_t) + needed);
  SE_CHECK_IF_ALLOCATED(a);
  a->blob_size = needed;
  if (needed > 0) {
    rc = nvs_get_blob(s_nvs, key, a->blob, &needed);
    if (rc != ESP_OK) {
      free(a);
      SE_FAIL(ERR_ESP_ERR, .esp_code = rc);
    }
  }
  *out       = a;
  *out_found = true;
  return NULL;
}

static SE_MUST_USE err_h nvs_save_action(uint8_t action_id, const sys_action_t* a) {
  char key[16];
  action_make_nvs_key(action_id, key, sizeof(key));
  size_t save_size = a->blob_size;
  if (save_size > CONFIG_SYS_ACTIONS_MAX_BLOB_SIZE) {
    save_size = CONFIG_SYS_ACTIONS_MAX_BLOB_SIZE;
  }
  SE_TRY_ESP(nvs_set_blob(s_nvs, key, a->blob, save_size));
  SE_TRY_ESP(nvs_commit(s_nvs));
  return NULL;
}

static SE_MUST_USE err_h nvs_erase(const char* key) {
  esp_err_t rc = nvs_erase_key(s_nvs, key);
  if (rc != ESP_OK && rc != ESP_ERR_NVS_NOT_FOUND) {
    SE_FAIL(ERR_ESP_ERR, .esp_code = rc);
  }
  SE_TRY_ESP(nvs_commit(s_nvs));
  return NULL;
}

/**
 * @brief Append one length-prefixed frame to a runtime action.
 *
 * Growth is limited to CONFIG_SYS_ACTIONS_MAX_BLOB_SIZE. On allocation failure,
 * @p io_action remains unchanged.
 *
 * @param io_action Action pointer that may be replaced by realloc().
 * @param frame Frame bytes to append.
 * @param len Frame length in bytes.
 * @return NULL on success, otherwise an error handle.
 */
static SE_MUST_USE err_h grow_and_append(sys_action_t** io_action, const uint8_t* frame, size_t len) {
  sys_action_t* a        = *io_action;
  size_t        old_size = a ? a->blob_size : 0;
  size_t        need     = sizeof(uint16_t) + len;

  if (old_size + need > CONFIG_SYS_ACTIONS_MAX_BLOB_SIZE) {
    SE_FAIL(ERR_BASE_NO_MEM, 0);
  }

  sys_action_t* bigger = realloc(a, sizeof(sys_action_t) + old_size + need);
  SE_CHECK_IF_ALLOCATED(bigger);

  uint16_t len16 = (uint16_t)len;
  memcpy(&bigger->blob[old_size], &len16, sizeof(len16));
  memcpy(&bigger->blob[old_size + sizeof(len16)], frame, len);
  bigger->blob_size = old_size + need;

  *io_action = bigger;
  return NULL;
}

/**
 * @brief Append a tapped frame to the active recording.
 *
 * The caller must hold @c s_actions_mutex so polling and appending remain
 * atomic with record-stop's final drain.
 *
 * @param frame Complete interface frame, including its class byte.
 * @param len Frame length in bytes.
 */
static void sys_actions_on_frame_locked(const uint8_t* frame, size_t len) {
  /** Exclude action-control packets, including the live record-stop command. */
  if (len == 0 || frame[0] == CONFIG_RX_PACKET_CLASS_SYS_ACTIONS) return;

  if (s_has_recording) {
    /** Recording is best-effort once the configured blob limit is reached. */
    SE_release(grow_and_append(&s_recording, frame, len));
  }
}

/**
 * @brief Poll and append captured frames outside the interface receiver task,
 * and run queued action requests (sys_actions_request()).
 *
 * @param arg Unused FreeRTOS task argument.
 */
static void sys_actions_tap_task(void* arg) {
  (void)arg;
  uint8_t frame[CONFIG_SYS_DATA_CONNECTOR_FRAME_MAX];

  while (1) {
    action_request_t req;
    while (R_QUEUE_RECEIVE(s_actions_requests, &req, 0) == pdTRUE) {
      SE_REPORT(sys_actions_invoke(req.scope, req.id));
    }

    size_t len = 0;
    R_MUTEX_LOCK(s_actions_mutex, WAIT_FOREVER);
    err_h err = sys_interface_tap_poll(frame, sizeof(frame), &len);
    if (SE_IS_OK(err) && len > 0) {
      sys_actions_on_frame_locked(frame, len);
    }
    R_MUTEX_UNLOCK(s_actions_mutex);

    if (SE_IS_ERR(err)) {
      SE_REPORT(err);
      continue;
    }
    if (len == 0) {
      /* Idle: wait for the next poll, or wake early for an action request. */
      (void)R_QUEUE_PEEK(s_actions_requests, &req, pdMS_TO_TICKS(CONFIG_SYS_ACTIONS_TAP_POLL_MS));
      continue;
    }
  }
}

err_h sys_actions_init(void) {
  SE_TRY_ESP(nvs_flash_init());
  SE_TRY_ESP(nvs_open(SYS_ACTIONS_NVS_NAMESPACE, NVS_READWRITE, &s_nvs));

  if (s_actions_tap_task_handle == NULL) {
    R_TASK_START(s_actions_tap_task_handle, sys_actions_tap_task, NULL, CONFIG_SYS_ACTIONS_TAP_TASK_PRIO);
    if (s_actions_tap_task_handle == NULL) {
      SE_FAIL(ERR_BASE_NO_MEM, 0);
    }
  }

  sys_actions_register_static();

  DBG(ESP_LOGI(TAG, "sys_actions initialized"));
  return NULL;
}

err_h sys_actions_bind_static(uint8_t action_id, action_static_func_t fn) {
  SE_CHECK_IN_RANGE(action_id, 1, CONFIG_SYS_ACTIONS_STATIC_SLOTS - 1);
  s_static_funcs[action_id] = fn;
  return NULL;
}

err_h sys_action_remove(uint8_t id) {
  SE_CHECK_IN_RANGE(id, 1, UINT8_MAX);
  char key[16];
  action_make_nvs_key(id, key, sizeof(key));
  SE_TRY(nvs_erase(key));
  DBG(ESP_LOGI(TAG, "removed dynamic action %u", id));
  return NULL;
}

err_h sys_action_remove_all(void) {
  /** Erasing can invalidate an iterator, so restart discovery after each key. */
  while (1) {
    nvs_iterator_t it                         = NULL;
    esp_err_t      rc                         = nvs_entry_find_in_handle(s_nvs, NVS_TYPE_BLOB, &it);
    char           key[NVS_KEY_NAME_MAX_SIZE] = {0};
    bool           have_target                = false;

    if (rc == ESP_OK) {
      nvs_entry_info_t info;
      nvs_entry_info(it, &info);
      strncpy(key, info.key, sizeof(key) - 1);
      have_target = true;
    }
    nvs_release_iterator(it);

    if (rc != ESP_OK && rc != ESP_ERR_NVS_NOT_FOUND) {
      SE_FAIL(ERR_ESP_ERR, .esp_code = rc);
    }
    if (!have_target) break;

    SE_TRY(nvs_erase(key));
  }

  DBG(ESP_LOGI(TAG, "removed all dynamic actions"));
  return NULL;
}

err_h sys_action_record_start(uint8_t id) {
  SE_CHECK_IN_RANGE(id, 1, UINT8_MAX);
  R_MUTEX_LOCK(s_actions_mutex, WAIT_FOREVER);
  bool busy = s_has_recording;
  R_MUTEX_UNLOCK(s_actions_mutex);
  if (busy) {
    SE_FAIL(ERR_ACTION_RECORDING_BUSY, id);
  }

  sys_action_t* a     = NULL;
  bool          found = false;
  SE_TRY(nvs_load_action(id, &a, &found));

  R_MUTEX_LOCK(s_actions_mutex, WAIT_FOREVER);
  if (s_has_recording) {
    R_MUTEX_UNLOCK(s_actions_mutex);
    free(a);
    SE_FAIL(ERR_ACTION_RECORDING_BUSY, id);
  }
  s_recording     = a;
  s_recording_id  = id;
  s_has_recording = true;
  R_MUTEX_UNLOCK(s_actions_mutex);

  sys_interface_tap_capture_start();

  DBG(ESP_LOGI(TAG, "recording dynamic action %u", id));
  return NULL;
}

err_h sys_action_record_stop(void) {
  sys_interface_tap_capture_end();

  R_MUTEX_LOCK(s_actions_mutex, WAIT_FOREVER);
  if (!s_has_recording) {
    R_MUTEX_UNLOCK(s_actions_mutex);
    return NULL;
  }

  /**
   * Capture is closed, so drain queued frames before detaching the recording.
   * The tap task uses the same mutex and cannot consume a frame between this
   * drain and the state transition below.
   */
  uint8_t frame[CONFIG_SYS_DATA_CONNECTOR_FRAME_MAX];
  while (1) {
    size_t len = 0;
    err_h drain_err = sys_interface_tap_poll(frame, sizeof(frame), &len);
    if (SE_IS_ERR(drain_err)) {
      R_MUTEX_UNLOCK(s_actions_mutex);
      sys_interface_tap_capture_start();
      return drain_err;
    }
    if (len == 0) break;
    sys_actions_on_frame_locked(frame, len);
  }

  sys_action_t* a  = s_recording;
  uint8_t       id = s_recording_id;
  s_recording      = NULL;
  s_has_recording  = false;
  R_MUTEX_UNLOCK(s_actions_mutex);

  DBG(ESP_LOGI(TAG, "stopped recording action %u", id));

  err_h err = nvs_save_action(id, a);
  free(a);
  SE_TRY(err);
  return NULL;
}

err_h sys_actions_request(uint8_t scope, uint8_t id) {
  if (scope > SYS_ACTION_SCOPE_DYNAMIC) {
    SE_FAIL(ERR_INVALID_VAL_UI32, .val = scope, .min = SYS_ACTION_SCOPE_STATIC, .max = SYS_ACTION_SCOPE_DYNAMIC);
  }
  SE_CHECK_IN_RANGE(id, 1, UINT8_MAX);
  const action_request_t req = {.scope = scope, .id = id};
  if (R_QUEUE_SEND(s_actions_requests, &req, 0) != pdTRUE) {
    SE_FAIL(ERR_ACTION_QUEUE_FULL, .action_id = id);
  }
  return NULL;
}

err_h sys_actions_invoke(uint8_t scope, uint8_t id) {
  SE_CHECK_IN_RANGE(id, 1, UINT8_MAX);
  if (scope == SYS_ACTION_SCOPE_STATIC) {
    if (id >= CONFIG_SYS_ACTIONS_STATIC_SLOTS || s_static_funcs[id] == NULL) {
      SE_FAIL(ERR_ACTION_NOT_FOUND, id);
    }
    return s_static_funcs[id]();
  }

  if (scope == SYS_ACTION_SCOPE_DYNAMIC) {
    sys_action_t* a     = NULL;
    bool          found = false;
    SE_TRY(nvs_load_action(id, &a, &found));

    if (!found) {
      free(a);
      SE_FAIL(ERR_ACTION_NOT_FOUND, id);
    }

    DBG(ESP_LOGI(TAG, "invoking dynamic action %u (%u bytes)", id, (unsigned)a->blob_size));
    sys_interface_suspend_rx();

    err_h  err = NULL;
    size_t off = 0;
    while (off + sizeof(uint16_t) <= a->blob_size) {
      uint16_t flen;
      memcpy(&flen, &a->blob[off], sizeof(flen));
      off += sizeof(flen);
      /** Stop safely if the persisted action has a corrupt or truncated tail. */
      if (off + flen > a->blob_size) break;
      err = sys_interface_decode(&a->blob[off], flen);
      off += flen;
      if (SE_IS_ERR(err)) break;
    }

    sys_interface_resume_rx();
    free(a);

    SE_TRY(err);
    return NULL;
  }
  SE_FAIL(ERR_INVALID_VAL_UI32, .val = scope, .min = SYS_ACTION_SCOPE_STATIC, .max = SYS_ACTION_SCOPE_DYNAMIC);
}

err_h sys_actions_handle_fault(err_h node, err_h chain) {
  (void)chain;
  if (!node || SE_get_tag_level(node->tag) != SE_LEVEL_CRITICAL) {
    return NULL;
  }
  // Severe actions fault: abort any active recording to prevent corrupted persistence
  R_MUTEX_LOCK(s_actions_mutex, WAIT_FOREVER);
  if (s_has_recording) {
    sys_interface_tap_capture_end();
    free(s_recording);
    s_recording     = NULL;
    s_has_recording = false;
  }
  R_MUTEX_UNLOCK(s_actions_mutex);
  return NULL;
}

