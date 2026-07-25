#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "sys_error.h"

#define MAX_DEVICE_ID 127

/* ========================================================================== *
 * 1. Context & Accessor Macros
 * ========================================================================== */

#define SYS_DEV_GET_ID(ctx) ((ctx)->base.device_id)
#define IF_SYS_DEV_FROZEN(ctx) if (((ctx))->base.is_frozen)
#define SYS_DEV_CTX_FREEZE(ctx) ((ctx))->base.is_frozen = true
#define SYS_DEV_CTX_UNFREEZE(ctx) ((ctx))->base.is_frozen = false

#define SYS_DEV_CHECK_HANDLE(handle, dev_id)   \
  do {                                         \
    if ((handle) == NULL) {                    \
      SE_RET_ERR(ERR_DEV_NO_HANDLE, (dev_id)); \
    }                                          \
  } while (0)

#define SYS_DEV_GET_ADAPTER_CONTEXT(ctx_type, hw_type, ctx_var, hw_var, input_handle) \
  ctx_type* ctx_var = (ctx_type*)(input_handle);                                      \
  SYS_DEV_CHECK_HANDLE(ctx_var, 0);                                                   \
  hw_type hw_var = (hw_type)(((ctx_var))->base.hw_handle);                            \
  SYS_DEV_CHECK_HANDLE(hw_var, ((ctx_var))->base.device_id)

/* ========================================================================== *
 * 2. Error & Failure Handling Macros
 * ========================================================================== */

#define RET_IF_DEV_ERR(err_ptr, ctx) SE_PASS_ON_ERR((err_ptr), ERR_DEV_DEP_FAILED, .dev_id = (ctx)->base.device_id)
#define RET_IF_DEV_INSTALL_FAIL(err_ptr, device_id) SE_PASS_ON_ERR((err_ptr), ERR_DEV_DEP_FAILED, .dev_id = (device_id))

#define SYS_DEV_CHECK_DRIVER_CALL(driver_call, ctx) RET_IF_DEV_ERR(SE_CONVERT_ESP(driver_call), (ctx))

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

/* ========================================================================== *
 * 3. Enums, Structs & Device Class Types
 * ========================================================================== */

typedef enum { SYS_DEVICE_CONTRACT_IO = 0, SYS_DEVICE_CONTRACT_POWER_VREG = 1, SYS_DEVICE_CONTRACT_POWER_MONITOR = 2, SYS_DEVICE_CONTRACT_POWER_USB_PD = 3, SYS_DEVICE_CONTRACT_MAX = 4 } sys_device_contract_type_e;

/**
 * @brief Device roles, as a bitmask - a device may hold several at once.
 *
 * A device's actual capabilities are its contracts; roles are the coarse
 * grouping. IO and PWR keep their historical numeric values (1 and 2).
 */
typedef enum sys_device_role_e {
  SYS_DEV_ROLE_NONE = 0,
  SYS_DEV_ROLE_IO = 1u << 0,
  SYS_DEV_ROLE_PWR = 1u << 1,
  SYS_DEV_ROLE_USER = 1u << 2,
} sys_device_role_e;

/*Lifecycle callbacks, shared by every instance of a device type*/
typedef struct sys_device_ops_t {
  err_h (*install)(const void* cfg, void** out_device_handle);
  err_h (*uninstall)(void* device_handle);
  err_h (*reset)(void* device_handle);
  err_h (*suspend)(void* device_handle);
  err_h (*resume)(void* device_handle);
  err_h (*freeze)(void* device_handle);
  err_h (*sync)(void* device_handle);
  err_h (*error_handler)(void* device_handle, err_h error);
} sys_device_ops_t;

/**
 * @brief Everything about a device TYPE that is a compile-time constant.
 *
 * Declared once per device as a `static const`. Contracts are declarative:
 * sys_device publishes them into the instance before running ops.install, so
 * an adapter does not call sys_io_register_driver / sys_power_register_* .
 *
 * @note `contracts` is void* (not const void*) because sys_io_vtable_t carries
 *       a mutable protected_pins field. The sys_power contracts are const and
 *       need a (void*) cast on the way in.
 */
typedef struct sys_device_class_t {
  const char* name;
  uint8_t roles; /* OR of sys_device_role_e bits */
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

/* ========================================================================== *
 * 4. Device State Query & Dispatch Macros
 * ========================================================================== */

#define SYS_DEV_IS_READY(d) ((d)->state == SYS_DEV_STATE_INSTALLED)
#define SYS_DEV_IS_INSTALLED(d) ((d)->state >= SYS_DEV_STATE_INSTALLED)
#define SYS_DEV_IS_SUSPENDED(d) ((d)->state == SYS_DEV_STATE_SUSPENDED)

typedef struct sys_device_t {
  uint8_t device_id;

  /*New-style devices: non-NULL class. Legacy devices: NULL, inline ptrs below.*/
  const sys_device_class_t* cls;
  void* cfg;       /* manager-owned heap copy of the device config */
  size_t cfg_size; /* 0 when there is no config */

  sys_device_state_e state;
  sys_device_role_e role;
  void* device_handle;
  const char* name;
  void** install_args;

  err_h (*install_device)(void** install_args, void** out_device_handle);
  err_h (*error_handler)(void* device_handle, err_h error);

  err_h (*uninstall_device)(void* device_handle);
  err_h (*reset_device)(void* device_handle);
  err_h (*suspend_device)(void* device_handle);
  err_h (*resume_device)(void* device_handle);
  err_h (*freeze_device)(void* device_handle);
  err_h (*sync_device)(void* device_handle);
  void* contracts[4];  // 0: IO, 1: POWER_VREG, 2: POWER_MONITOR, 3: POWER_USB_PD
} sys_device_t;

#define SYS_DEV_GET_CONTRACT(dev, type) ((dev) ? (dev)->contracts[(type)] : NULL)

#define IF_SYS_DEV_AND_FEATURE(device_id, contract_type, contract_struct_type, func_member, dev_ptr, vtable_ptr)                                            \
  for (sys_device_t* dev_ptr = sys_device_get_by_id((device_id)); dev_ptr; dev_ptr = NULL)                                                                  \
    for (const contract_struct_type* vtable_ptr = (const contract_struct_type*)SYS_DEV_GET_CONTRACT(dev_ptr, contract_type); vtable_ptr; vtable_ptr = NULL) \
      if (SYS_DEV_IS_READY(dev_ptr) && vtable_ptr->func_member)

#define SYS_DEV_DISPATCH(dev_id, contract_enum, contract_type, func_name, ...)        \
  do {                                                                                \
    sys_device_t* __disp_dev = sys_device_get_by_id((dev_id));                        \
    if (__disp_dev == NULL) {                                                         \
      SE_RET_ERR(ERR_DEV_NOT_FOUND, (dev_id));                                        \
    }                                                                                 \
    if (!SYS_DEV_IS_INSTALLED(__disp_dev)) {                                          \
      SE_RET_ERR(ERR_DEV_NOT_INSTALLED, (dev_id));                                    \
    }                                                                                 \
    if (SYS_DEV_IS_SUSPENDED(__disp_dev)) {                                           \
      SE_RET_ERR(ERR_DEV_SUSPENDED, (dev_id));                                        \
    }                                                                                 \
    contract_type* __vtable = (contract_type*)__disp_dev->contracts[contract_enum];   \
    if (__vtable == NULL || __vtable->func_name == NULL) {                            \
      SE_RET_ERR(ERR_DEV_FEATURE_UNAVAILABLE, (dev_id), (uint8_t)(contract_enum), 0); \
    }                                                                                 \
    SE_RET_IF_ERR(__vtable->func_name(__disp_dev->device_handle, ##__VA_ARGS__));     \
    return NULL;                                                                      \
  } while (0)

/* ========================================================================== *
 * 5. Adapter Construction & Installation Step Helpers
 * ========================================================================== */

typedef struct {
  void* hw_handle;
  uint8_t device_id;
  bool is_frozen;
  uint16_t steps_done; /* adapter-defined bitmask of completed install steps */
} sys_device_adapter_base_t;

/**
 * Record / test an install step. Lets teardown roll back exactly what was
 * built, instead of inferring it from whether a field still holds a sentinel.
 */
#define SYS_DEV_STEP_DONE(ctx, bit) ((ctx)->base.steps_done |= (1u << (bit)))
#define IF_SYS_DEV_STEP_DONE(ctx, bit) if ((ctx)->base.steps_done & (1u << (bit)))

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

/*Requires cfg_ptr's first member to be `uint8_t device_id`*/
#define SYS_DEVICE_CREATE(cls_ptr, cfg_ptr) sys_device_install_cfg((cls_ptr), (cfg_ptr)->device_id, (cfg_ptr), sizeof(*(cfg_ptr)))

/* ========================================================================== *
 * 6. Function Declarations
 * ========================================================================== */

/**
 * @brief Create new device, after run new device will be automatically
 * installed via provided "install_device" function
 *
 * @deprecated Legacy path. New devices use sys_device_install_cfg /
 *             SYS_DEVICE_CREATE with a typed config struct.
 */
err_h sys_device_install(sys_device_t* device);

/**
 * @brief Install a device from a static class plus a typed config struct.
 *
 * sys_device takes a heap copy of `cfg` (it only ever knows a pointer and a
 * length - never the device's field layout), publishes `cls->contracts[]`
 * into the instance, then runs `cls->ops.install`. The copy is released when
 * the device is uninstalled, or immediately if install fails.
 *
 * Prefer the SYS_DEVICE_CREATE() wrapper, which derives device_id and size.
 */
err_h sys_device_install_cfg(const sys_device_class_t* cls, uint8_t device_id, const void* cfg, size_t cfg_size);

/**
 * @brief Uninstall created device, device will be completely removed
 */
err_h sys_device_uninstall(uint8_t device_id);

/**
 * @brief Run privided reset function
 */
err_h sys_device_reset(uint8_t device_id);

/**
 * @brief suspend and save current state
 */
err_h sys_device_suspend(uint8_t device_id);

/**
 * @brief resume from saved state
 */
err_h sys_device_resume(uint8_t device_id);

/**
 * @brief suspend all devices
 */
err_h sys_device_suspend_all(void);

/**
 * @brief resume all devices
 */
err_h sys_device_resume_all(void);

/**
 * @brief freeze device updates and save actions in internal driver state
 */
err_h sys_device_freeze(uint8_t device_id);

/**
 * @brief unfreeze sync device actions like read / write that are supported in "frozen state"
 */
err_h sys_device_sync(uint8_t device_id);

/**
 * @brief freeze all devices
 */
err_h sys_device_freeze_all(void);

/**
 * @brief sync all devices
 */
err_h sys_device_sync_all(void);

sys_device_t* sys_device_get_by_id(uint8_t device_id);
