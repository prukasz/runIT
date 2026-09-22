#include "sys_device.h"
#include "sys_error.h"
#include "sys_error_log.h"
#include "utils.h"

static const char* TAG = __FILE_NAME__;

const char* const sys_device_contract_type_e_to_string[] = {"IO", "POWER_VREG", "POWER_MONITOR", "POWER_USB_PD", "HBRIDGE"};

/*Registry mutations (install / uninstall) are init/config context only.
  Reads are lock-free: sys_device_get_by_id() sits on the hot dispatch path and
  is reachable from ISR-adjacent code, where a mutex cannot be taken.*/
static sys_device_t* s_device_registry[CONFIG_SYS_DEVICE_MAX_ID + 1] = {NULL};
/* Error policy registered by the application (sys_device_register_error_policy).
   Without one, a CRITICAL device error suspends every device. */
static sys_device_error_policy_f s_error_policy;

static SE_MUST_USE err_h default_error_policy(uint8_t device_id, se_level_e level, uint8_t action_scope, uint8_t action_id, err_h error) {
  (void)device_id;
  (void)action_scope;
  (void)action_id;
  (void)error;
  return (level == SE_LEVEL_CRITICAL) ? sys_device_suspend_all() : NULL;
}

void sys_device_register_error_policy(sys_device_error_policy_f policy) {
  s_error_policy = policy;
}
#define DEV_OP(d, f) ((d)->cls->ops.f)
#define DEV_NAME(d) ((d)->cls->name)

/* Every error leaving a device carries its device_id: wrap a lifecycle-op
 * result as ERR_DEV_DEP_FAILED(dev_id) so device policy can attribute it.
 * NULL (success) passes through untouched. */
#define DEV_WRAP(err, dev_id)                                  \
  ({                                                           \
    err_h __wrap_err = (err);                                  \
    __wrap_err ? SE_WRAP_DEV_ERR(__wrap_err, (dev_id)) : NULL; \
  })

/* Runs a device op, or reports ERR_BASE_NOT_SUPPORTED when the class lacks
 * it, and wraps the result with the device's id. */
#define DEV_RUN_OP(dev, fn)                                                          \
  DEV_WRAP((fn) ? (fn)((dev)->device_handle) : SE_ERR_NEW(ERR_BASE_NOT_SUPPORTED, 0), \
           (dev)->device_id)

/* Shared skeleton for a single-device op that requires the device to be
 * active (found + installed + not suspended, via SYS_DEV_REQUIRE_ACTIVE) and
 * errors with ERR_BASE_NOT_SUPPORTED if op_field isn't implemented. Used by
 * reset/freeze/sync, none of which change dev->state on success. */
#define SYS_DEV_LIFECYCLE_OP(device_id, op_field, verb, log_level)                  \
  do {                                                                              \
    sys_device_t* __disp_dev = sys_device_get_by_id((device_id));                   \
    SYS_DEV_REQUIRE_ACTIVE(__disp_dev, (device_id));                                \
    err_h (*__disp_fn)(void*) = DEV_OP(__disp_dev, op_field);                       \
    ESP_LOG_LEVEL((log_level), TAG, "%s device: %s", (verb), DEV_NAME(__disp_dev)); \
    return DEV_RUN_OP(__disp_dev, __disp_fn);                                       \
  } while (0)

/* Shared skeleton for suspend/resume: only found+installed is required (not
 * SYS_DEV_REQUIRE_ACTIVE - toggling suspend is exactly what these two do), an
 * idempotent no-op if the device is already in the target state (skip_expr,
 * may reference __disp_dev), and dev->state is set to new_state on success. */
#define SYS_DEV_LIFECYCLE_TOGGLE(device_id, op_field, verb, log_level, skip_expr, new_state) \
  do {                                                                                       \
    sys_device_t* __disp_dev = sys_device_get_by_id((device_id));                            \
    if (!__disp_dev) SE_FAIL(ERR_DEV_NOT_FOUND, (device_id));                             \
    if (!SYS_DEV_IS_INSTALLED(__disp_dev)) SE_FAIL(ERR_DEV_NOT_INSTALLED, (device_id));   \
    if (skip_expr) return NULL;                                                              \
    err_h (*__disp_fn)(void*) = DEV_OP(__disp_dev, op_field);                                \
    ESP_LOG_LEVEL((log_level), TAG, "%s device: %s", (verb), DEV_NAME(__disp_dev));          \
    err_h __disp_ret = DEV_RUN_OP(__disp_dev, __disp_fn);                                    \
    if (__disp_ret) return __disp_ret;                                                       \
    __disp_dev->state = (new_state);                                                         \
    return NULL;                                                                             \
  } while (0)
/* Shared skeleton for a MAX_DEVICE_ID sweep: silently skips devices that
 * aren't eligible (eligible_expr, may reference __disp_dev) or don't
 * implement op_field, and optionally updates dev->state on each success
 * (new_state, or SYS_DEV_STATE_NONE to leave it untouched). log_before
 * reproduces sys_device_reset_all()'s pre-call log line.
 *
 * Best effort: a failing device doesn't stop the sweep - suspend_all is the
 * fault safe-state path and must reach every device. The first failure is
 * returned; later ones go to diagnostics and are released. Every failure is
 * wrapped with its device_id (DEV_WRAP).
 *
 * `reverse` picks sweep direction. Device ids are assigned in dependency
 * order - a device's sys_io_pin_ref_t (oe_pin/rst_pin/en_pin/...) always
 * points at a lower-id device (see runit_board_cfg.c) - so a
 * dependent's own op can call back into a lower-id device's sys_io while
 * that op runs (e.g. tca6424a's suspend drives its rst_pin low, which is a
 * gpio_esp pin). Going low-to-high id would suspend the dependency (low id)
 * before the dependent (high id) gets a chance to touch it, failing with
 * ERR_DEV_SUSPENDED. Convention: tear-down ops (suspend, uninstall) sweep
 * high id -> low id so dependents finish before their dependencies go down;
 * bring-up ops (resume) sweep low -> high so dependencies are already up
 * when a dependent resumes. */
#define SYS_DEV_LIFECYCLE_OP_ALL(op_field, verb_gerund, verb_base, eligible_expr, log_before, new_state, reverse) \
  do {                                                                                                            \
    err_h __first_err = NULL;                                                                                     \
    for (int __k = 0; __k <= CONFIG_SYS_DEVICE_MAX_ID; __k++) {                                                   \
      int __i = (reverse) ? (CONFIG_SYS_DEVICE_MAX_ID - __k) : __k;                                               \
      sys_device_t* __disp_dev = sys_device_get_by_id((uint8_t)__i);                                              \
      if (!__disp_dev || !(eligible_expr)) continue;                                                              \
      err_h (*__disp_fn)(void*) = DEV_OP(__disp_dev, op_field);                                                   \
      if (!__disp_fn) continue;                                                                                   \
      if (log_before) ESP_LOGW(TAG, "%s device: %s", (verb_gerund), DEV_NAME(__disp_dev));                        \
      err_h __disp_ret = DEV_RUN_OP(__disp_dev, __disp_fn);                                                       \
      if (__disp_ret) {                                                                                           \
        ESP_LOGE(TAG, "Failed to %s device: %s", (verb_base), DEV_NAME(__disp_dev));                              \
        sweep_keep_first(&__first_err, __disp_ret);                                                               \
        continue;                                                                                                 \
      }                                                                                                           \
      if ((new_state) != SYS_DEV_STATE_NONE) __disp_dev->state = (new_state);                                     \
    }                                                                                                             \
    return __first_err;                                                                                           \
  } while (0)

/* Keeps the first sweep failure for the caller; any later one is sent to
 * diagnostics (it already carries its device_id) and released. */
static void sweep_keep_first(err_h* first, err_h err) {
  if (*first == NULL) {
    *first = err;
    return;
  }
  SE_log(err);
}

#undef OWNER
#define OWNER OWNER_SYS_DEVICE_INSTALL
err_h sys_device_install_cfg(const sys_device_class_t* cls, uint8_t device_id, const void* cfg, size_t cfg_size) {
  SE_CHECK_NOT_NULL(cls);
  SE_CHECK_NOT_NULL(cls->ops.install);
  SE_CHECK_IN_RANGE(device_id, 0, CONFIG_SYS_DEVICE_MAX_ID);

  if (s_device_registry[device_id] != NULL) {
    SE_FAIL(ERR_DEV_ALREADY_EXIST, device_id);
  }

  sys_device_t* new_dev = (sys_device_t*)calloc(1, sizeof(sys_device_t));
  SE_CHECK_IF_ALLOCATED(new_dev);

  /*Heap-copy the config: the caller's struct is typically a stack compound
    literal that dies as soon as create() returns.*/
  if (cfg != NULL && cfg_size > 0) {
    new_dev->cfg = malloc(cfg_size);
    if (new_dev->cfg == NULL) {
      free(new_dev);
      SE_FAIL(ERR_BASE_NO_MEM, 0);
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
    return SE_WRAP_ERR(install_status, ERR_DEV_INSTALL_FAILED, .dev_id = device_id);
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
err_h sys_device_report_error_with_level(uint8_t device_id, se_level_e level, err_h error) {
  if (!SE_is_valid_error_ptr(error)) return NULL;

  sys_device_t* dev = sys_device_get_by_id(device_id);

  /* User rule: when device importance is NONE (test device), ignore all error handling */
  if (dev && dev->importance == SYS_DEV_IMPORTANCE_NONE) {
    return NULL;
  }

  SE_CHECK_IN_RANGE((unsigned)level, SE_LEVEL_NONE, SE_LEVEL_CRITICAL);
  if (level == SE_LEVEL_NONE) return NULL;

  /* Non-critical errors obey device importance clamping */
  if (level != SE_LEVEL_CRITICAL) {
    if (!dev) {
      return NULL;
    }
    if ((uint8_t)level > (uint8_t)dev->importance) {
      level = (se_level_e)dev->importance;
    }
    if (level == SE_LEVEL_NONE) return NULL;
  }

  /* CRITICAL or clamped level dispatch */
  uint8_t action_scope = dev ? dev->actions[level].scope : 0;
  uint8_t action_id    = dev ? dev->actions[level].id : 0;
  return (s_error_policy ? s_error_policy : default_error_policy)(device_id, level, action_scope, action_id, error);
}

err_h sys_device_report_error(uint8_t device_id, err_h error) {
  return SE_is_valid_error_ptr(error)
             ? sys_device_report_error_with_level(device_id, SE_get_tag_level(error->tag), error) : NULL;
}

bool sys_device_is_ignored(uint8_t device_id) {
  sys_device_t* dev = sys_device_get_by_id(device_id);
  return (dev != NULL) && (dev->importance == SYS_DEV_IMPORTANCE_NONE);
}

#undef OWNER
#define OWNER OWNER_SYS_DEVICE_SET_ERROR_HANDLING
err_h sys_device_set_error_handling(uint8_t device_id, sys_device_importance_e importance, const uint8_t actions[5]) {
  sys_device_t* dev = sys_device_get_by_id(device_id);
  if (!dev) {
    SE_FAIL(ERR_DEV_NOT_FOUND, device_id);
  }
  SE_CHECK_IN_RANGE((uint32_t)importance, SYS_DEV_IMPORTANCE_NONE, SYS_DEV_IMPORTANCE_CRITICAL);

  if (actions) {
    SE_CHECK_IN_RANGE(actions[0], 0, 0x0f);
    for (int i = 1; i < 5; i++) {
      uint8_t scope = (actions[0] & (1u << (i - 1))) ? 0x01 : 0x00;
      unsigned limit = (scope == 0x01) ? CONFIG_SYS_ACTIONS_ID_SPACE : CONFIG_SYS_ACTIONS_STATIC_SLOTS;
      SE_CHECK_IN_RANGE(actions[i], 0, limit - 1);
      dev->actions[i].scope = scope;
      dev->actions[i].id    = actions[i];
    }
  } else {
    memset(dev->actions, 0, sizeof(dev->actions));
  }
  dev->importance = importance;
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
    err_h err = fn ? DEV_WRAP(fn(dev->device_handle), device_id) : NULL;
    ESP_LOGW(TAG, "Deleted device: %s", DEV_NAME(dev));
    /*Released only after the adapter has torn down - it may still be reading
      its own copy of the config until then.*/
    free(dev->cfg);
    free(dev);
    return err;
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
  err_h first_err = NULL;
  for (int i = CONFIG_SYS_DEVICE_MAX_ID; i >= 0; i--) {
    if (!s_device_registry[i]) continue;
    err_h err = sys_device_uninstall((uint8_t)i);
    if (err) sweep_keep_first(&first_err, err);
  }
  return first_err;
}

#undef OWNER
#define OWNER OWNER_SYS_DEVICE_SET_ONBOARD
err_h sys_device_set_onboard(uint8_t device_id) {
  sys_device_t* dev = sys_device_get_by_id(device_id);
  if (dev == NULL) SE_FAIL(ERR_DEV_NOT_FOUND, device_id);
  dev->onboard = true;
  return NULL;
}

#undef OWNER
#define OWNER OWNER_SYS_DEVICE_USER_UNINSTALL
err_h sys_device_user_uninstall(uint8_t device_id) {
  sys_device_t* dev = sys_device_get_by_id(device_id);
  if (dev != NULL && dev->onboard) SE_FAIL(ERR_DEV_ONBOARD, device_id);
  return sys_device_uninstall(device_id);
}

#undef OWNER
#define OWNER OWNER_SYS_DEVICE_USER_UNINSTALL_ALL
err_h sys_device_user_uninstall_all(void) {
  // Same order as sys_device_uninstall_all(). User devices depend on onboard
  // ones (pins on the TCA6424A, PCA9685, ...), never the other way round, so
  // skipping onboard devices leaves no dangling dependency.
  err_h first_err = NULL;
  for (int i = CONFIG_SYS_DEVICE_MAX_ID; i >= 0; i--) {
    if (!s_device_registry[i] || s_device_registry[i]->onboard) continue;
    err_h err = sys_device_uninstall((uint8_t)i);
    if (err) sweep_keep_first(&first_err, err);
  }
  return first_err;
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
