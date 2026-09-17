#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "sys_error.h"
#include <sdkconfig.h>
typedef enum { SYS_DEVICE_CONTRACT_IO = 0, SYS_DEVICE_CONTRACT_POWER_VREG = 1, SYS_DEVICE_CONTRACT_POWER_MONITOR = 2, SYS_DEVICE_CONTRACT_POWER_USB_PD = 3, SYS_DEVICE_CONTRACT_MAX = 4 } sys_device_contract_type_e;

/**
 * @brief String form of sys_device_contract_type_e, indexed by contract_id -
 * used e.g. by sys_error_dev.h's ERR_DEV_FEATURE_UNAVAILABLE description.
 * sys_error_dev.h forward-declares this same extern rather than including
 * this header, since it's parsed too early in the include chain to safely
 * pull in sys_device.h - see that file's comment. Kept in sync with
 * SYS_DEVICE_CONTRACT_IO's value (0) via the static_assert below; if the
 * enum is ever renumbered, sys_error_dev.h's ERR_DEV_FEATURE_UNAVAILABLE
 * logger (which checks contract_id == 0 as a literal, for the same reason)
 * needs updating too.
 */
extern const char* const sys_device_contract_type_e_to_string[];
_Static_assert(SYS_DEVICE_CONTRACT_IO == 0, "sys_error_dev.h's ERR_DEV_FEATURE_UNAVAILABLE logger assumes SYS_DEVICE_CONTRACT_IO == 0");

/*Lifecycle callbacks, shared by every instance of a device type*/
typedef struct sys_device_ops_t {
  err_h (*install)(const void* cfg, void** out_device_handle);
  err_h (*uninstall)(void* device_handle);
  err_h (*reset)(void* device_handle);
  err_h (*suspend)(void* device_handle);
  err_h (*resume)(void* device_handle);
  err_h (*freeze)(void* device_handle);
  err_h (*sync)(void* device_handle);
} sys_device_ops_t;

/**
 * @brief Everything about a device TYPE that is a compile-time constant.
 *
 * Declared once per device as a `static const`. Contracts are declarative and
 * live only here - `sys_device_t` looks them up via `dev->cls->contracts[]`,
 * so an adapter does not call sys_io_register_driver / sys_power_register_* .
 */

typedef struct sys_device_class_t {
  const char* name;
  void* contracts[SYS_DEVICE_CONTRACT_MAX];
  sys_device_ops_t ops;
} sys_device_class_t;

/**
 * @brief Device lifecycle state.
 *
 * INSTALLING is registry-findable but not READY: an adapter's dependencies may
 * look the device up mid-install, while dispatch still refuses it because the
 * handle is not set yet. Ordering matters - SYS_DEV_IS_INSTALLED tests >=.
 */
typedef enum sys_device_state_e {
  SYS_DEV_STATE_NONE = 0,
  SYS_DEV_STATE_INSTALLING,
  SYS_DEV_STATE_INSTALLED,
  SYS_DEV_STATE_SUSPENDED,
} sys_device_state_e;

/**
 * @brief Device importance level.
 *
 * Determines whether error handling is active (NONE disables it) and caps
 * the maximum error severity that this device can escalate to.
 */
typedef enum sys_device_importance_e {
  SYS_DEV_IMPORTANCE_NONE = 0,
  SYS_DEV_IMPORTANCE_LOW = 1,
  SYS_DEV_IMPORTANCE_MEDIUM = 2,
  SYS_DEV_IMPORTANCE_HIGH = 3,
  SYS_DEV_IMPORTANCE_CRITICAL = 4,
} sys_device_importance_e;

/**
 * sys_device_err_level_e (SYS_DEV_ERR_NONE .. CRITICAL) is defined in
 * sys_error_base.h so that all error tags can embed default severity levels.
 */

/** Stage of a device-fault response, included in structured failure errors. */
typedef enum sys_device_fault_stage_e {
  SYS_DEV_FAULT_STAGE_VM_STOP = 0,
  SYS_DEV_FAULT_STAGE_FREEZE = 1,
  SYS_DEV_FAULT_STAGE_ACTION = 2,
} sys_device_fault_stage_e;

/**
 * @brief Application coordinator error policy for classified device errors.
 *
 * Implemented statically by the application layer (runit). A weak fallback is
 * provided in sys_device.c that freezes all devices on CRITICAL errors.
 */
err_h sys_device_app_error_policy(uint8_t device_id,
                                  sys_device_err_level_e level,
                                  uint8_t action_id,
                                  err_h error);

/**
 * @brief Main device object with all necessary data and structures
 */
typedef struct sys_device_t {
  uint8_t device_id;

  const sys_device_class_t* cls;
  void* cfg;       /* manager-owned heap copy of the device config */
  size_t cfg_size; /* 0 when there is no config */

  sys_device_state_e state;
  void* device_handle;

  /**
   * @brief Per-instance error handling mode - see sys_device_report_error().
   */
  uint8_t actions[5];           /* sys_actions ids indexed by sys_device_err_level_e */
  sys_device_importance_e importance; /* NONE disables error handling; clamps max error level */
} sys_device_t;

/**
 * @brief common for all devices header structure that should be included on top of ctx with name 'base'
 */
typedef struct {
  void* hw_handle;
  uint8_t device_id;
  bool is_frozen;
  uint16_t steps_done; /* adapter-defined bitmask of completed install steps */
} sys_device_adapter_base_t;

/**
 * @brief Install a device from a static class plus a typed config struct.
 *
 * sys_device takes a heap copy of `cfg` (it only ever knows a pointer and a
 * length - never the device's field layout), sets `cls` on the instance, then
 * runs `cls->ops.install`. Contracts are looked up via `dev->cls->contracts[]`
 *
 * Prefer the SYS_DEVICE_CREATE() wrapper, which derives device_id and size.
 */
err_h sys_device_install_cfg(const sys_device_class_t* cls, uint8_t device_id, const void* cfg, size_t cfg_size);

/*Requires cfg_ptr's first member to be `uint8_t device_id`*/
#define SYS_DEVICE_CREATE(cls_ptr, cfg_ptr) sys_device_install_cfg((cls_ptr), (cfg_ptr)->device_id, (cfg_ptr), sizeof(*(cfg_ptr)))

err_h sys_device_uninstall(uint8_t device_id);
err_h sys_device_uninstall_all(void);
err_h sys_device_reset(uint8_t device_id);
err_h sys_device_reset_all(void);
err_h sys_device_suspend(uint8_t device_id);
err_h sys_device_resume(uint8_t device_id);
err_h sys_device_suspend_all(void);
err_h sys_device_resume_all(void);
err_h sys_device_freeze(uint8_t device_id);
err_h sys_device_sync(uint8_t device_id);
err_h sys_device_freeze_all(void);
err_h sys_device_sync_all(void);

sys_device_t* sys_device_get_by_id(uint8_t device_id);

/**
 * @brief Report an error that occurred on device_id to the centralized error policy.
 *
 * The fault severity level is determined automatically from the root error's tag
 * (via SE_get_tag_level()).
 *
 * If dev->importance is SYS_DEV_IMPORTANCE_NONE, all error handling is ignored
 * (treated as a test/non-essential device; no actions or latches are triggered).
 *
 * If effective severity is SYS_DEV_ERR_CRITICAL and importance != NONE:
 * it unconditionally latches the fault in the VM, halts the VM, freezes all devices,
 * and invokes dev->actions[SYS_DEV_ERR_CRITICAL].
 *
 * For non-critical errors (LOW, MEDIUM, HIGH):
 * - If the level exceeds dev->importance, it is clamped down to dev->importance.
 * - Dispatches dev->actions[level] to the application policy.
 *
 * @param device_id Target device.
 * @param error Error handle (must not be NULL).
 * @return NULL on successful dispatch or if ignored, or a structured policy error.
 */
err_h sys_device_report_error(uint8_t device_id, err_h error);

/**
 * @brief Report an error with an explicit severity level override.
 */
err_h sys_device_report_error_with_level(uint8_t device_id, sys_device_err_level_e level, err_h error);

/**
 * @brief Returns true if device has SYS_DEV_IMPORTANCE_NONE (ignored test device).
 */
bool sys_device_is_ignored(uint8_t device_id);

/**
 * @brief Set device_id's per-instance error handling mode in one call.
 *
 * @param device_id Target device; must already be registered.
 * @param importance New value for sys_device_t.importance (NONE disables handling).
 * @param actions Copied into dev->actions[5]; each entry must be
 *                < CONFIG_SYS_ACTIONS_ID_SPACE. Pass NULL to leave
 *                actions[] zeroed (equivalent to {0, 0, 0, 0, 0}).
 * @return err_h NULL on success, ERR_DEV_NOT_FOUND if device_id isn't
 *               registered, or ERR_INVALID_VAL_UI32 if an actions[] entry is
 *               out of range.
 */
err_h sys_device_set_error_handling(uint8_t device_id, sys_device_importance_e importance, const uint8_t actions[5]);

/* ========================================================================== *
 * Field accessors - helpers
 * ========================================================================== */

#define SYS_DEV_GET_ID(ctx) ((ctx)->base.device_id)
#define IF_SYS_DEV_FROZEN(ctx) if (((ctx))->base.is_frozen)
#define SYS_DEV_CTX_FREEZE(ctx) ((ctx))->base.is_frozen = true
#define SYS_DEV_CTX_UNFREEZE(ctx) ((ctx))->base.is_frozen = false
#define SYS_DEV_IS_READY(d) ((d)->state == SYS_DEV_STATE_INSTALLED)
#define SYS_DEV_IS_INSTALLED(d) ((d)->state >= SYS_DEV_STATE_INSTALLED)
#define SYS_DEV_IS_SUSPENDED(d) ((d)->state == SYS_DEV_STATE_SUSPENDED)
#define SYS_DEV_GET_CONTRACT(dev, type) ((dev) ? (dev)->cls->contracts[(type)] : NULL)
/**
 * Record / test an install step. Lets teardown roll back exactly what was
 * built, instead of inferring it from whether a field still holds a sentinel.
 */
#define SYS_DEV_STEP_DONE(ctx, bit) ((ctx)->base.steps_done |= (1u << (bit)))
#define IF_SYS_DEV_STEP_DONE(ctx, bit) if ((ctx)->base.steps_done & (1u << (bit)))

/* ========================================================================== *
 * Safety checks and error operations
 * ========================================================================== */
#define SYS_DEV_CHECK_DRIVER_CALL(driver_call, ctx) RET_IF_DEV_ERR(SE_CONVERT_ESP(driver_call), (ctx))
#define RET_IF_DEV_ERR(err_ptr, ctx) SE_PASS_ON_ERR((err_ptr), ERR_DEV_DEP_FAILED, .dev_id = (ctx)->base.device_id)
#define RET_IF_DEV_INSTALL_FAIL(err_ptr, device_id) SE_PASS_ON_ERR((err_ptr), ERR_DEV_DEP_FAILED, .dev_id = (device_id))

#define SYS_DEV_CHECK_HANDLE(handle, dev_id)   \
  do {                                         \
    if ((handle) == NULL) {                    \
      SE_RET_ERR(ERR_DEV_NO_HANDLE, (dev_id)); \
    }                                          \
  } while (0)

/* ========================================================================== *
 * Integrated code blocks - contract operations
 * ========================================================================== */

#define SYS_DEV_GET_ADAPTER_CONTEXT(ctx_type, hw_type, ctx_var, hw_var, input_handle) \
  ctx_type* ctx_var = (ctx_type*)(input_handle);                                      \
  SYS_DEV_CHECK_HANDLE(ctx_var, 0);                                                   \
  hw_type hw_var = (hw_type)(((ctx_var))->base.hw_handle);                            \
  SYS_DEV_CHECK_HANDLE(hw_var, ((ctx_var))->base.device_id)

#define IF_SYS_DEV_AND_FEATURE(device_id, contract_type, contract_struct_type, func_member, dev_ptr, vtable_ptr)                                            \
  for (sys_device_t* dev_ptr = sys_device_get_by_id((device_id)); dev_ptr; dev_ptr = NULL)                                                                  \
    for (const contract_struct_type* vtable_ptr = (const contract_struct_type*)SYS_DEV_GET_CONTRACT(dev_ptr, contract_type); vtable_ptr; vtable_ptr = NULL) \
      if (SYS_DEV_IS_READY(dev_ptr) && vtable_ptr->func_member)

/**
 * @brief Guard for a device pointer already fetched via sys_device_get_by_id().
 * Returns ERR_DEV_NOT_FOUND / ERR_DEV_NOT_INSTALLED / ERR_DEV_SUSPENDED from the
 * calling function if the device isn't installed and un-suspended. Shared by
 * SYS_DEV_DISPATCH and any hand-rolled dispatch that needs the same check
 * wrapped around non-vtable logic (e.g. sys_power's budget accounting).
 */
#define SYS_DEV_REQUIRE_ACTIVE(dev, device_id)                    \
  do {                                                            \
    if ((dev) == NULL) {                                          \
      SE_RET_ERR(ERR_DEV_NOT_FOUND, (device_id));                 \
    }                                                              \
    if (!SYS_DEV_IS_INSTALLED(dev)) {                              \
      SE_RET_ERR(ERR_DEV_NOT_INSTALLED, (device_id));              \
    }                                                              \
    if (SYS_DEV_IS_SUSPENDED(dev)) {                                \
      SE_RET_ERR(ERR_DEV_SUSPENDED, (device_id));                  \
    }                                                              \
  } while (0)

/**
 * @brief Can handle most function from contract by invoking selected function with provided arguments
 * Integrated error handling
 */

#define SYS_DEV_DISPATCH(dev_id, contract_enum, contract_type, func_name, ...)           \
  do {                                                                                   \
    sys_device_t* __disp_dev = sys_device_get_by_id((dev_id));                           \
    SYS_DEV_REQUIRE_ACTIVE(__disp_dev, (dev_id));                                        \
    contract_type* __vtable = (contract_type*)__disp_dev->cls->contracts[contract_enum]; \
    if (__vtable == NULL || __vtable->func_name == NULL) {                               \
      SE_RET_ERR(ERR_DEV_FEATURE_UNAVAILABLE, (dev_id), (uint8_t)(contract_enum), 0);    \
    }                                                                                    \
    SE_RET_IF_ERR(__vtable->func_name(__disp_dev->device_handle, ##__VA_ARGS__));        \
    return NULL;                                                                         \
  } while (0)

/* ========================================================================== *
 * Integrated code blocks - Device instalation
 * ========================================================================== */

// Call once, from the `fail:` label of an install_device implementation, after
// `err` has been set to the failing step's error and before returning.
// Rolls back any partially-constructed state via `uninstall_fn`, suspending
// error reporting for the duration (teardown failures here are noise next to
// the real cause), then reports `err` wrapped with `device_id` attached.
//
// Contract for uninstall_fn (same function used for real sys_device_uninstall()):
//  - must tolerate any subset of ctx's fields being zero/unset (calloc'd but
//    not yet populated this far into install)
//  - must NOT early-return on an individual teardown step failing; accumulate
//    the first error if you want to report one, but always free everything
#define SYS_DEV_INSTALL_FAIL(err, device_id, out_handle, uninstall_fn, ctx)                             \
  do {                                                                                                  \
    *(out_handle) = NULL;                                                                               \
    if ((ctx) != NULL) {                                                                                \
      ESP_LOGW(TAG, "Install failed for device %u, rolling back partial state", (unsigned)(device_id)); \
      SE_suspend();                                                                                     \
      (uninstall_fn)((ctx));                                                                            \
      SE_resume();                                                                                      \
    }                                                                                                   \
    RET_IF_DEV_INSTALL_FAIL((err), (device_id));                                                        \
  } while (0)

/**
 * Allocate + prime an adapter context from its typed config.
 *
 * Requires: ctx_type's first member is `sys_device_adapter_base_t base`,
 * ctx_type has a `cfg` member of the config's type, and the config's first
 * member is `uint8_t device_id`.
 *
 * @note Declares ctx_var, so it cannot sit inside an unbraced `if`, and must
 *       appear before any `goto fail` that would jump over it.
 */
#define SYS_DEV_CTX_NEW(ctx_type, ctx_var, cfg_ptr)           \
  ctx_type* ctx_var = (ctx_type*)calloc(1, sizeof(ctx_type)); \
  SE_CHECK_IF_ALLOCATED(ctx_var);                             \
  (ctx_var)->cfg = *(cfg_ptr);                                \
  (ctx_var)->base.device_id = (cfg_ptr)->device_id

/*Run one install step; on failure log it and jump to the rollback label.
  Requires `err_h err`, a `fail:` label and `TAG` in scope.*/
#define SYS_DEV_INSTALL_STEP(expr, what)           \
  do {                                             \
    err = (expr);                                  \
    if (SE_IS_ERR(err)) {                          \
      ESP_LOGE(TAG, "install: %s failed", (what)); \
      goto fail;                                   \
    }                                              \
  } while (0)

/*Run one teardown step, keeping the FIRST error. Never early-returns: teardown
  must always free everything, so a failing step may not abort the rest.*/
#define SYS_DEV_TEARDOWN_STEP(err_acc, expr)                  \
  do {                                                        \
    err_h __r = (expr);                                       \
    if (SE_IS_ERR(__r) && SE_IS_OK(err_acc)) (err_acc) = __r; \
  } while (0)
