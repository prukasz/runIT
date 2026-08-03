#include "sys_device.h"
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "sys_error.h"
#include "sys_io.h"
#include "utils.h"

static const char* TAG = __FILE_NAME__;

const char* const sys_device_contract_type_e_to_string[] = {"IO", "POWER_VREG", "POWER_MONITOR", "POWER_USB_PD"};

/*Registry mutations (install / uninstall) are init/config context only.
  Reads are lock-free: sys_device_get_by_id() sits on the hot dispatch path and
  is reachable from ISR-adjacent code, where a mutex cannot be taken.*/
static sys_device_t* s_device_registry[CONFIG_SYS_DEVICE_MAX_ID + 1] = {NULL};

// Registers sys_device_report_error() as sys_errors' device-error hook (see
// sys_error.h) at load time, per the [[runit]] skill's static-construction
// convention - lets sys_error_handler_task dispatch device-owned chains
// without sys_errors depending on sys_device (which already depends on
// sys_errors, so the reverse would be circular).
__attribute__((constructor)) static void sys_device_register_error_hook(void) {
  SE_register_device_error_hook(sys_device_report_error);
}

#define DEV_OP(d, f) ((d)->cls->ops.f)
#define DEV_NAME(d) ((d)->cls->name)

/* Shared skeleton for a single-device op that requires the device to be
 * active (found + installed + not suspended, via SYS_DEV_REQUIRE_ACTIVE) and
 * errors with ERR_BASE_NOT_SUPPORTED if op_field isn't implemented. Used by
 * reset/freeze/sync, none of which change dev->state on success. */
#define SYS_DEV_LIFECYCLE_OP(device_id, op_field, verb, log_level)                  \
  do {                                                                              \
    sys_device_t* __disp_dev = sys_device_get_by_id((device_id));                   \
    SYS_DEV_REQUIRE_ACTIVE(__disp_dev, (device_id));                                \
    err_h (*__disp_fn)(void*) = DEV_OP(__disp_dev, op_field);                       \
    if (!__disp_fn) {                                                               \
      SE_RET_ERR(ERR_BASE_NOT_SUPPORTED, 0);                                        \
    }                                                                               \
    ESP_LOG_LEVEL((log_level), TAG, "%s device: %s", (verb), DEV_NAME(__disp_dev)); \
    SE_RET_IF_ERR(__disp_fn(__disp_dev->device_handle));                            \
    return NULL;                                                                    \
  } while (0)

/* Shared skeleton for suspend/resume: only found+installed is required (not
 * SYS_DEV_REQUIRE_ACTIVE - toggling suspend is exactly what these two do), an
 * idempotent no-op if the device is already in the target state (skip_expr,
 * may reference __disp_dev), and dev->state is set to new_state on success. */
#define SYS_DEV_LIFECYCLE_TOGGLE(device_id, op_field, verb, log_level, skip_expr, new_state) \
  do {                                                                                       \
    sys_device_t* __disp_dev = sys_device_get_by_id((device_id));                            \
    if (!__disp_dev) SE_RET_ERR(ERR_DEV_NOT_FOUND, (device_id));                             \
    if (!SYS_DEV_IS_INSTALLED(__disp_dev)) SE_RET_ERR(ERR_DEV_NOT_INSTALLED, (device_id));   \
    if (skip_expr) return NULL;                                                              \
    err_h (*__disp_fn)(void*) = DEV_OP(__disp_dev, op_field);                                \
    if (!__disp_fn) SE_RET_ERR(ERR_BASE_NOT_SUPPORTED, 0);                                   \
    ESP_LOG_LEVEL((log_level), TAG, "%s device: %s", (verb), DEV_NAME(__disp_dev));          \
    SE_RET_IF_ERR(__disp_fn(__disp_dev->device_handle));                                     \
    __disp_dev->state = (new_state);                                                         \
    return NULL;                                                                             \
  } while (0)
/* Shared skeleton for a MAX_DEVICE_ID sweep: silently skips devices that
 * aren't eligible (eligible_expr, may reference __disp_dev) or don't
 * implement op_field, aborts the sweep and returns on the first failure, and
 * optionally updates dev->state on each success (new_state, or
 * SYS_DEV_STATE_NONE to leave it untouched). log_before reproduces
 * sys_device_reset_all()'s pre-call log line - the only _all variant that has
 * one; the rest only log on failure.
 *
 * `reverse` picks sweep direction. Device ids are assigned in dependency
 * order - a device's sys_io_pin_ref_t (oe_pin/rst_pin/en_pin/...) always
 * points at a lower-id device (see runit_board_devices.h) - so a
 * dependent's own op can call back into a lower-id device's sys_io while
 * that op runs (e.g. tca6424a's suspend drives its rst_pin low, which is a
 * gpio_esp pin). Going low-to-high id would suspend the dependency (low id)
 * before the dependent (high id) gets a chance to touch it, failing with
 * ERR_DEV_SUSPENDED. Convention: tear-down ops (suspend, uninstall) sweep
 * high id -> low id so dependents finish before their dependencies go down;
 * bring-up ops (resume) sweep low -> high so dependencies are already up
 * when a dependent resumes. */
#define SYS_DEV_LIFECYCLE_OP_ALL(op_field, verb_gerund, verb_base, eligible_expr, log_before, new_state, reverse)  \
  do {                                                                                                             \
    for (int __k = 0; __k <= CONFIG_SYS_DEVICE_MAX_ID; __k++) {                                                    \
      int __i = (reverse) ? (CONFIG_SYS_DEVICE_MAX_ID - __k) : __k;                                                \
      sys_device_t* __disp_dev = sys_device_get_by_id((uint8_t)__i);                                               \
      if (!__disp_dev || !(eligible_expr)) continue;                                                               \
      err_h (*__disp_fn)(void*) = DEV_OP(__disp_dev, op_field);                                                    \
      if (!__disp_fn) continue;                                                                                    \
      if (log_before) ESP_LOGW(TAG, "%s device: %s", (verb_gerund), DEV_NAME(__disp_dev));                         \
      err_h __disp_ret = __disp_fn(__disp_dev->device_handle);                                                     \
      if (SE_IS_ERR(__disp_ret)) {                                                                                 \
        ESP_LOGE(TAG, "Failed to %s device: %s", (verb_base), DEV_NAME(__disp_dev));                               \
        SE_RET_IF_ERR(__disp_ret);                                                                                 \
      }                                                                                                            \
      if ((new_state) != SYS_DEV_STATE_NONE) __disp_dev->state = (new_state);                                      \
    }                                                                                                              \
    return NULL;                                                                                                   \
  } while (0)

#undef OWNER
#define OWNER OWNER_SYS_DEVICE_INSTALL
err_h sys_device_install_cfg(const sys_device_class_t* cls, uint8_t device_id, const void* cfg, size_t cfg_size) {
  SE_CHECK_NOT_NULL(cls);
  SE_CHECK_NOT_NULL(cls->ops.install);
  SE_CHECK_IN_RANGE(device_id, 0, CONFIG_SYS_DEVICE_MAX_ID);

  if (s_device_registry[device_id] != NULL) {
    SE_RET_ERR(ERR_DEV_ALREADY_EXIST, device_id);
  }

  sys_device_t* new_dev = (sys_device_t*)calloc(1, sizeof(sys_device_t));
  SE_CHECK_IF_ALLOCATED(new_dev);

  /*Heap-copy the config: the caller's struct is typically a stack compound
    literal that dies as soon as create() returns.*/
  if (cfg != NULL && cfg_size > 0) {
    new_dev->cfg = malloc(cfg_size);
    if (new_dev->cfg == NULL) {
      free(new_dev);
      SE_RET_ERR(ERR_BASE_NO_MEM, 0);
    }
    memcpy(new_dev->cfg, cfg, cfg_size);
    new_dev->cfg_size = cfg_size;
  }

  new_dev->device_id = device_id;
  new_dev->cls = cls;

  /*cls is set before install: adapters (and their dependencies) may look this
    device up mid-install and read dev->cls->contracts[]. The state stays
    INSTALLING until install succeeds, so SYS_DEV_DISPATCH still refuses it in
    the meantime.*/
  new_dev->state = SYS_DEV_STATE_INSTALLING;
  s_device_registry[device_id] = new_dev;

  ESP_LOGI(TAG, "Installing device: %s (ID: %u)", cls->name, device_id);

  err_h install_status = cls->ops.install(new_dev->cfg, &new_dev->device_handle);

  if (SE_IS_ERR(install_status)) {
    ESP_LOGE(TAG, "Failed to install %s (ID: %u)", cls->name, device_id);
    s_device_registry[device_id] = NULL;
    free(new_dev->cfg);
    free(new_dev);
    SE_RET_IF_ERR(install_status);
  }

  new_dev->state = SYS_DEV_STATE_INSTALLED;

  return NULL;
}

#undef OWNER
#define OWNER OWNER_SYS_DEVICE_GET_BY_ID
sys_device_t* sys_device_get_by_id(uint8_t device_id) {
  if (device_id > CONFIG_SYS_DEVICE_MAX_ID) return NULL;
  sys_device_t* found_device = s_device_registry[device_id];
  return found_device;
}

#undef OWNER
#define OWNER OWNER_SYS_DEVICE_REPORT_ERROR
err_h sys_device_report_error(uint8_t device_id, err_h error) {
  sys_device_t* dev = sys_device_get_by_id(device_id);
  if (!dev) return NULL;
  if (!dev->generate_error_callback && !dev->use_error_handler) return NULL;
  if (!dev->cls->ops.error_handler) return NULL;
  return dev->cls->ops.error_handler(dev->device_handle, error);
}

#undef OWNER
#define OWNER OWNER_SYS_DEVICE_SET_ERROR_HANDLING
err_h sys_device_set_error_handling(uint8_t device_id, bool use_error_handler, bool generate_error_callback, const uint8_t actions[3]) {
  sys_device_t* dev = sys_device_get_by_id(device_id);
  if (!dev) {
    SE_RET_ERR(ERR_DEV_NOT_FOUND, device_id);
  }

  if (actions) {
    for (int i = 0; i < 3; i++) {
      SE_CHECK_IN_RANGE(actions[i], 0, CONFIG_SYS_ACTIONS_ID_SPACE - 1);
    }
  }

  dev->use_error_handler = use_error_handler;
  dev->generate_error_callback = generate_error_callback;
  if (actions) {
    memcpy(dev->actions, actions, sizeof(dev->actions));
  } else {
    memset(dev->actions, 0, sizeof(dev->actions));
  }
  return NULL;
}

#undef OWNER
#define OWNER OWNER_SYS_DEVICE_RESET
err_h sys_device_reset(uint8_t device_id) {
  SYS_DEV_LIFECYCLE_OP(device_id, reset, "Resetting", ESP_LOG_WARN);
}

#undef OWNER
#define OWNER OWNER_SYS_DEVICE_UNINSTALL
err_h sys_device_uninstall(uint8_t device_id) {
  SE_CHECK_IN_RANGE(device_id, 0, CONFIG_SYS_DEVICE_MAX_ID);

  sys_device_t* dev = s_device_registry[device_id];

  if (dev != NULL) {
    dev->state = SYS_DEV_STATE_NONE;
    s_device_registry[device_id] = NULL;  // Zwolnienie indeksu

    err_h (*fn)(void*) = DEV_OP(dev, uninstall);
    if (fn) {
      fn(dev->device_handle);
    }
    ESP_LOGW(TAG, "Deleted device: %s", DEV_NAME(dev));
    /*Released only after the adapter has torn down - it may still be reading
      its own copy of the config until then.*/
    free(dev->cfg);
    free(dev);
    return NULL;
  }
  return NULL;
}

#undef OWNER
#define OWNER OWNER_SYS_DEVICE_RESET_ALL
err_h sys_device_reset_all(void) {
  SYS_DEV_LIFECYCLE_OP_ALL(reset, "Resetting", "reset", SYS_DEV_IS_READY(__disp_dev), true, SYS_DEV_STATE_NONE, false);
}

#undef OWNER
#define OWNER OWNER_SYS_DEVICE_UNINSTALL_ALL
err_h sys_device_uninstall_all(void) {
  // High id -> low id, same dependency-order reasoning as
  // SYS_DEV_LIFECYCLE_OP_ALL's reverse sweep: device_uninstall() releases
  // pin-ref locks on whatever lower-id device it depends on (e.g.
  // tca6424a's rst_pin lives on gpio_esp), so a dependent must finish
  // uninstalling before its dependency is torn down.
  for (int i = CONFIG_SYS_DEVICE_MAX_ID; i >= 0; i--) {
    if (!s_device_registry[i]) continue;
    SE_RET_IF_ERR(sys_device_uninstall((uint8_t)i));
  }
  return NULL;
}

#undef OWNER
#define OWNER OWNER_SYS_DEVICE_SUSPEND
err_h sys_device_suspend(uint8_t device_id) {
  SYS_DEV_LIFECYCLE_TOGGLE(device_id, suspend, "Suspending", ESP_LOG_INFO, SYS_DEV_IS_SUSPENDED(__disp_dev), SYS_DEV_STATE_SUSPENDED);
}

#undef OWNER
#define OWNER OWNER_SYS_DEVICE_RESUME
err_h sys_device_resume(uint8_t device_id) {
  SYS_DEV_LIFECYCLE_TOGGLE(device_id, resume, "Resuming", ESP_LOG_INFO, !SYS_DEV_IS_SUSPENDED(__disp_dev), SYS_DEV_STATE_INSTALLED);
}

#undef OWNER
#define OWNER OWNER_SYS_DEVICE_SUSPEND_ALL
err_h sys_device_suspend_all(void) {
  SYS_DEV_LIFECYCLE_OP_ALL(suspend, "Suspending", "suspend", SYS_DEV_IS_READY(__disp_dev), false, SYS_DEV_STATE_SUSPENDED, true);
}

#undef OWNER
#define OWNER OWNER_SYS_DEVICE_RESUME_ALL
err_h sys_device_resume_all(void) {
  SYS_DEV_LIFECYCLE_OP_ALL(resume, "Resuming", "resume", SYS_DEV_IS_SUSPENDED(__disp_dev), false, SYS_DEV_STATE_INSTALLED, false);
}

#undef OWNER
#define OWNER OWNER_SYS_DEVICE_FREEZE
err_h sys_device_freeze(uint8_t device_id) {
  SYS_DEV_LIFECYCLE_OP(device_id, freeze, "Freezing", ESP_LOG_DEBUG);
}

#undef OWNER
#define OWNER OWNER_SYS_DEVICE_SYNC
err_h sys_device_sync(uint8_t device_id) {
  SYS_DEV_LIFECYCLE_OP(device_id, sync, "Syncing", ESP_LOG_DEBUG);
}

#undef OWNER
#define OWNER OWNER_SYS_DEVICE_FREEZE_ALL
err_h sys_device_freeze_all(void) {
  SYS_DEV_LIFECYCLE_OP_ALL(freeze, "Freezing", "freeze", SYS_DEV_IS_READY(__disp_dev), false, SYS_DEV_STATE_NONE, false);
}

#undef OWNER
#define OWNER OWNER_SYS_DEVICE_SYNC_ALL
err_h sys_device_sync_all(void) {
  SYS_DEV_LIFECYCLE_OP_ALL(sync, "Syncing", "sync", SYS_DEV_IS_READY(__disp_dev), false, SYS_DEV_STATE_NONE, false);
}
