#pragma once
#include <stddef.h>
#include <string.h>
#include "sys_error.h"
#include <sdkconfig.h>
#include <stdint.h>
//#ref-enum @alias Device Contract
typedef enum {
  SYS_DEVICE_CONTRACT_IO = 0, //@alias Digital/Analog IO @description Basic pin control - read or drive a pin, or measure/generate a voltage on it
  SYS_DEVICE_CONTRACT_POWER_VREG = 1, //@alias Voltage Regulator @description An adjustable power output - turn it on/off and set its voltage and current limit
  SYS_DEVICE_CONTRACT_POWER_MONITOR = 2, //@alias Power Monitor @description Measures voltage and current on a power rail, with optional over-current alerts
  SYS_DEVICE_CONTRACT_POWER_USB_PD = 3, //@alias USB-C Power Delivery @description Negotiates power (voltage/current) from a USB-C charger
  SYS_DEVICE_CONTRACT_HBRIDGE = 4, //@alias Motor Driver (H-Bridge) @description Drives a DC motor forward, backward, or brakes it
  SYS_DEVICE_CONTRACT_MAX = 5 //@alias (internal) @description Not a real contract - marks the end of the list, used internally for bounds checking
} sys_device_contract_type_e;

/**
 * @brief How a contract describes itself to the device layer.
 *
 * Each contract module owns the names of its functions (the NULL-terminated
 * `<contract>_feature_names[]`, in vtable member order); runit registers them once,
 * at boot, so the error log can say "feature set_level unavailable on contract IO"
 * without sys_device or sys_error_dev.h knowing any contract module.
 * The enum above stays the stable id of a contract (slot in `cls->contracts[]`,
 * published to the app).
 */
SE_MUST_USE err_h sys_device_register_contract(sys_device_contract_type_e id, const char* name, const char* const* feature_names);
/** Name of a registered contract, or NULL. */
const char* sys_device_contract_name(uint8_t contract_id);
/** Name of a contract function (feature id = vtable member index), or NULL. */
const char* sys_device_feature_name(uint8_t contract_id, uint8_t feature_id);

/*Lifecycle callbacks, shared by every instance of a device type*/
typedef struct sys_device_ops_t {
  err_h (*install)(const void* cfg, void** out_device_handle);
  err_h (*uninstall)(void* device_handle);
  err_h (*reset)(void* device_handle);
  err_h (*suspend)(void* device_handle);
  err_h (*resume)(void* device_handle);
} sys_device_ops_t;

/**
 * Packet router - headers the system does not know
 * -------------------------------------------------
 * Frame (class 0x01, header byte first):
 *   [0x00][type_id][cfg...]              create; cfg[0] is the device_id (the wire cfg struct)
 *   [0x80..0xFF][device_id][args...]     operation of the device itself
 * Everything in between belongs to the fixed system packets (uninstall, io, power, ...).
 */
#define SYS_DEVICE_OP_CREATE 0x00
#define SYS_DEVICE_OP_CUSTOM_FIRST 0x80
#define SYS_DEVICE_MAX_TYPES 16

/** Room for the data an operation hands back; the caller decides where it goes (sys_interface_respond for a packet). */
typedef struct sys_device_reply_t {
  uint8_t* buf;
  size_t cap;
  size_t len;
} sys_device_reply_t;

/** Appends `len` bytes to the reply. ERR_INVALID_VAL (range) when it doesn't fit. */
SE_MUST_USE err_h sys_device_reply_put(sys_device_reply_t* reply, const void* data, size_t len);

/** An operation of one device type. `args` is exactly `size` bytes; fill `reply` for a getter. */
typedef err_h (*sys_device_op_f)(void* device_handle, const uint8_t* args, size_t len, sys_device_reply_t* reply);

typedef struct sys_device_op_t {
  uint8_t op;         /* SYS_DEVICE_OP_CUSTOM_FIRST..0xFF */
  uint8_t size;       /* exact size of the argument bytes */
  bool read_only;     /* allowed while the device is suspended (it changes nothing) */
  sys_device_op_f fn;
} sys_device_op_t;

/* One row of a class's op table: SYS_DEVICE_OP(0x80, servo_home_t, op_home). */
#define SYS_DEVICE_OP(code, arg_type, handler) {.op = (code), .size = sizeof(arg_type), .read_only = false, .fn = (handler)}
#define SYS_DEVICE_OP_RO(code, arg_type, handler) {.op = (code), .size = sizeof(arg_type), .read_only = true, .fn = (handler)}
/* Both class fields: `.contracts = {...}, SYS_DEVICE_OPS(s_ops)` inside the class initializer. */
#define SYS_DEVICE_OPS(table) .dev_ops = (table), .dev_ops_count = (uint8_t)(sizeof(table) / sizeof((table)[0]))
/* Reads `args` into a local of `arg_type` (no alignment assumptions on the rx buffer). */
#define SYS_DEVICE_OP_ARGS(arg_type, name, args) \
  arg_type name;                                 \
  memcpy(&name, (args), sizeof(arg_type))

/**
 * @brief Everything about a device TYPE that is a compile-time constant.
 *
 * Declared once per device as a `static const`. Contracts are declarative and
 * live only here - `sys_device_t` looks them up via `dev->cls->contracts[]`,
 * so an adapter does not call sys_io_register_driver / sys_power_register_* .
 */

typedef struct sys_device_class_t {
  const char* name;
  const void* contracts[SYS_DEVICE_CONTRACT_MAX]; /* static const vtables, kept in flash */
  sys_device_ops_t ops;
  const sys_device_op_t* dev_ops; /* the device's own operations (router), NULL when none */
  uint8_t dev_ops_count;
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
 * @brief Device action reference containing scope and action ID.
 */
typedef struct {
  uint8_t scope; /**< SYS_ACTION_SCOPE_STATIC (0x00) or SYS_ACTION_SCOPE_DYNAMIC (0x01) */
  uint8_t id;    /**< Action ID in selected scope (0 = disabled) */
} sys_device_action_t;

/**
 * @brief Device importance level.
 *
 * Determines the minimum error severity handled by the device policy.
 * CRITICAL errors go through at every importance except NONE, which ignores
 * every error of the device (the user's choice, for a device under test).
 * A new device starts at LOW.
 */
//#ref-enum @alias Device Error Importance
typedef enum sys_device_importance_e {
  SYS_DEV_IMPORTANCE_NONE = 0, //@alias Disabled @description Ignores every error of the device, critical ones included: nothing stops the system when it fails. Only for a device under test.
  SYS_DEV_IMPORTANCE_LOW = 1, //@alias Low @description Handles only critical device errors. Every device starts here.
  SYS_DEV_IMPORTANCE_MEDIUM = 2, //@alias Medium @description Handles high and critical device errors.
  SYS_DEV_IMPORTANCE_HIGH = 3, //@alias High @description Handles medium, high and critical device errors.
  SYS_DEV_IMPORTANCE_CRITICAL = 4, //@alias Critical @description Handles every device error severity.
} sys_device_importance_e;

/**
 * se_level_e (SE_LEVEL_NONE .. CRITICAL) is defined in
 * sys_error_base.h so that all error tags can embed default severity levels.
 */

/** Stage of a device-fault response, included in structured failure errors. */
typedef enum sys_device_fault_stage_e {
  SYS_DEV_FAULT_STAGE_VM_STOP = 0,
  SYS_DEV_FAULT_STAGE_SUSPEND = 1,
  SYS_DEV_FAULT_STAGE_ACTION = 2,
} sys_device_fault_stage_e;

/**
 * @brief Application policy for a classified device error: the response
 * (for example VM stop + suspend all on CRITICAL) plus the device's configured
 * action (action_scope / action_id). Returns an owned response failure or NULL.
 */
typedef err_h (*sys_device_error_policy_f)(uint8_t device_id, se_level_e level, uint8_t action_scope, uint8_t action_id, err_h error);

/** @brief Register the policy (runit, at boot). Without one, CRITICAL suspends every device. */
void sys_device_register_error_policy(sys_device_error_policy_f policy);

/**
 * @brief Main device object with all necessary data and structures
 */
typedef struct sys_device_t {
  uint8_t device_id;
  uint8_t state;
  bool onboard;


  uint64_t io_locked_pins;

  const sys_device_class_t* cls;

  void* device_handle;

  /**
   * @brief Per-instance error handling mode - see sys_device_report_error().
   */
  sys_device_action_t actions[5];       /* indexed by se_level_e (1..4) */
  sys_device_importance_e importance; /* NONE ignores every error; LOW (the start value) handles critical ones; higher handles more severities */

} sys_device_t;

/**
 * @brief common for all devices header structure that should be included on top of ctx with name 'base'
 */
typedef struct {
  void* hw_handle;
  uint8_t device_id;
  uint16_t steps_done; /* adapter-defined bitmask of completed install steps */
} sys_device_adapter_base_t;

/**
 * @brief Install a device from a static class plus a typed config struct.
 *
 * Sets `cls` on the instance, then runs `cls->ops.install(cfg, ...)`. The manager
 * keeps no copy of `cfg`: it is valid for the call only, so install copies what it
 * needs into the device's own state. Contracts are looked up via `dev->cls->contracts[]`.
 *
 * Prefer the SYS_DEVICE_CREATE() wrapper, which derives device_id.
 */
SE_MUST_USE err_h sys_device_install(const sys_device_class_t* cls, uint8_t device_id, const void* cfg);

/*Requires cfg_ptr's first member to be `uint8_t device_id`*/
#define SYS_DEVICE_CREATE(cls_ptr, cfg_ptr) sys_device_install((cls_ptr), (cfg_ptr)->device_id, (cfg_ptr))

/**
 * @brief Register how a device type is created from the wire.
 *
 * @param type_id   The byte after 0x00 in a create frame.
 * @param cfg_size  Exact size of the wire cfg (its first byte is the device_id).
 * @param create    Builds the device from the cfg bytes (validates, then d_<chip>_create()).
 *
 * Boot only (runit registers every type before the interface starts).
 */
typedef err_h (*sys_device_create_f)(const void* cfg);
SE_MUST_USE err_h sys_device_register_type(uint8_t type_id, size_t cfg_size, sys_device_create_f create);

/**
 * Wrapper so a device's own `err_h d_x_create(const d_x_cfg_t*)` can be registered with no
 * decoder: the cfg struct is the wire struct (`__packed`, alignment 1, device_id first).
 * File scope: SYS_DEVICE_TYPE_FN(d_x_cfg_t, d_x_create), then
 * sys_device_register_type(type_id, sizeof(d_x_cfg_t), type_create_d_x_create).
 */
#define SYS_DEVICE_TYPE_FN(cfg_type, create_fn)   static SE_MUST_USE err_h type_create_##create_fn(const void* cfg) { return create_fn((const cfg_type*)cfg); }

/**
 * @brief Route one frame: create (0x00) or an operation of a device (0x80..0xFF).
 *
 * @param frame  Bytes starting at the header byte (the class byte is already stripped).
 * @param reply  Filled by an operation that returns data; `reply->len` is reset to 0 first.
 * @return NULL, ERR_DEV_TYPE_UNKNOWN / ERR_DEV_CREATE_SIZE (create), ERR_DEV_NOT_FOUND /
 *         ERR_DEV_NOT_INSTALLED / ERR_DEV_SUSPENDED / ERR_DEV_OP_UNKNOWN / ERR_DEV_OP_SIZE,
 *         or the operation's own error wrapped with the device id.
 */
SE_MUST_USE err_h sys_device_route(const uint8_t* frame, size_t len, sys_device_reply_t* reply);

SE_MUST_USE err_h sys_device_uninstall(uint8_t device_id);
SE_MUST_USE err_h sys_device_uninstall_all(void);

/**
 * @brief Mark an installed device as onboard (baked onto the PCB).
 *
 * Called by the board config right after it installs each onboard device.
 * The flag lives until the device is uninstalled (by the system: hard reset,
 * shutdown), so a board reinstall marks it again.
 *
 * @return NULL on success, ERR_DEV_NOT_FOUND if nothing is installed at device_id.
 */
SE_MUST_USE err_h sys_device_set_onboard(uint8_t device_id);

/*User entry points (inbound packets, replayed recorded actions). The firmware
  only protects onboard devices from being uninstalled; every other limit on
  what a user may do with them is app policy.*/

/**
 * @brief sys_device_uninstall() for users.
 * @return ERR_DEV_ONBOARD {dev_id} for an onboard device, otherwise as sys_device_uninstall().
 */
SE_MUST_USE err_h sys_device_user_uninstall(uint8_t device_id);

/**
 * @brief sys_device_uninstall_all() for users: uninstalls every device except
 * onboard ones (same high-to-low order, best effort, first error returned).
 */
SE_MUST_USE err_h sys_device_user_uninstall_all(void);
SE_MUST_USE err_h sys_device_reset(uint8_t device_id);
SE_MUST_USE err_h sys_device_reset_all(void);
SE_MUST_USE err_h sys_device_suspend(uint8_t device_id);
SE_MUST_USE err_h sys_device_resume(uint8_t device_id);
SE_MUST_USE err_h sys_device_suspend_all(void);
SE_MUST_USE err_h sys_device_resume_all(void);

sys_device_t* sys_device_get_by_id(uint8_t device_id);

/**
 * @brief Report an error that occurred on device_id to the centralized error policy.
 *
 * The fault severity level is determined automatically from the supplied error node's tag
 * (via SE_get_tag_level()).
 *
 * If dev->importance is SYS_DEV_IMPORTANCE_NONE, every error is ignored,
 * critical ones included (a device under test; no actions are triggered, the
 * chain is still logged).
 *
 * If the severity is SE_LEVEL_CRITICAL (any importance but NONE): it latches
 * the fault in the VM, halts the VM, suspends all devices, and invokes
 * dev->actions[SE_LEVEL_CRITICAL].
 *
 * For non-critical errors (LOW, MEDIUM, HIGH), importance is a minimum
 * severity threshold: LOW handles none, MEDIUM handles HIGH, HIGH handles
 * MEDIUM and HIGH, and CRITICAL handles all. An admitted error dispatches
 * dev->actions[level] at its original severity.
 *
 * @param device_id Target device.
 * @param error Error handle (must not be NULL).
 * @return NULL on successful dispatch or if ignored, or a structured policy error.
 */
SE_MUST_USE err_h sys_device_report_error(uint8_t device_id, err_h error);

/**
 * @brief Report an error with an explicit severity level override.
 */
SE_MUST_USE err_h sys_device_report_error_with_level(uint8_t device_id, se_level_e level, err_h error);

/**
 * @brief Returns true if device has SYS_DEV_IMPORTANCE_NONE (ignored test device: none of its errors are handled).
 */
bool sys_device_is_ignored(uint8_t device_id);

/**
 * @brief Set device_id's per-instance error handling mode in one call.
 *
 * @param device_id Target device; must already be registered.
 * @param importance New value for sys_device_t.importance (NONE disables handling).
 * @param actions Copied into dev->actions[5]; each entry must be
 *                an ID in its selected static/dynamic scope. actions[0] holds
 *                scope bits 0..3 for LOW..CRITICAL (1 = dynamic). Pass NULL to leave
 *                actions[] zeroed (equivalent to {0, 0, 0, 0, 0}).
 * @return err_h NULL on success, ERR_DEV_NOT_FOUND if device_id isn't
 *               registered, or ERR_INVALID_VAL_UI32 if an actions[] entry is
 *               out of range.
 */
SE_MUST_USE err_h sys_device_set_error_handling(uint8_t device_id, sys_device_importance_e importance, const uint8_t actions[5]);

/* ========================================================================== *
 * Field accessors - helpers
 * ========================================================================== */

#define SYS_DEV_GET_ID(ctx) ((ctx)->base.device_id)
/* Declares `ctx_var` from the handle an op or contract function receives. No NULL
   check: sys_device never calls an op without its handle (validate once). */
#define SYS_DEV_CTX_FROM(ctx_type, ctx_var, handle) ctx_type* ctx_var = (ctx_type*)(handle)
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
/* Calls a driver function (esp_err_t) from an adapter. A failure becomes
   ERR_DEV_DRIVER_FAILED {dev_id, line} over ERR_ESP_ERR {code}: the device,
   the exact adapter call site (owner = device type) and the ESP code. */
/* Builds (does not return) the same ERR_DEV_DRIVER_FAILED {dev_id, line} over
   ERR_ESP_ERR {code} chain for an esp_err_t obtained elsewhere - an ESP-IDF
   call made directly by an adapter, or a failure reported from a driver task. */
#define SYS_DEV_DRIVER_ERR(esp_err, ctx)                                                      \
  SE_WRAP_ERR(SE_ERR_NEW(ERR_ESP_ERR, .esp_code = (esp_err)), ERR_DEV_DRIVER_FAILED, \
              .dev_id = SYS_DEV_GET_ID(ctx), .line = __LINE__)
#define SYS_DEV_CHECK_DRIVER_CALL(driver_call, ctx) \
  SE_TRY_WRAP(SE_CONVERT_ESP(driver_call), ERR_DEV_DRIVER_FAILED, .dev_id = SYS_DEV_GET_ID(ctx), .line = __LINE__)
#define SYS_DEV_TRY(err_ptr, ctx) SE_TRY_WRAP((err_ptr), ERR_DEV_DEP_FAILED, .dev_id = (ctx)->base.device_id)

#define SYS_DEV_CHECK_HANDLE(handle, dev_id)   \
  do {                                         \
    if ((handle) == NULL) {                    \
      SE_FAIL(ERR_DEV_NO_HANDLE, (dev_id)); \
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
      SE_FAIL(ERR_DEV_NOT_FOUND, (device_id));                 \
    }                                                              \
    if (!SYS_DEV_IS_INSTALLED(dev)) {                              \
      SE_FAIL(ERR_DEV_NOT_INSTALLED, (device_id));              \
    }                                                              \
    if (SYS_DEV_IS_SUSPENDED(dev)) {                                \
      SE_FAIL(ERR_DEV_SUSPENDED, (device_id));                  \
    }                                                              \
  } while (0)

/**
 * @brief Can handle most function from contract by invoking selected function with provided arguments
 * Integrated error handling
 */

/* Feature id of a contract function: its index in the contract struct (every
   member is a function pointer). Reported in ERR_DEV_FEATURE_UNAVAILABLE and
   named by the contract's <contract>_feature_names[] table. */
#define SYS_DEV_FEATURE_ID(contract_type, func_name) \
  ((uint8_t)(offsetof(contract_type, func_name) / sizeof(void (*)(void))))

/* Resolves device_id to an active device (SYS_DEV_REQUIRE_ACTIVE) whose
   contract implements func_name, otherwise returns the error
   (ERR_DEV_FEATURE_UNAVAILABLE {dev, contract, feature}). Declares dev_var and
   contract_var in the enclosing scope. The single copy of the dispatch
   checks: SYS_DEV_DISPATCH, sys_io's pin-lock dispatch and sys_power's
   budgeted calls all start here. */
#define SYS_DEV_RESOLVE(device_id, contract_enum, contract_type, func_name, dev_var, contract_var)                               \
  sys_device_t* dev_var = sys_device_get_by_id((device_id));                                                                  \
  SYS_DEV_REQUIRE_ACTIVE(dev_var, (device_id));                                                                               \
  const contract_type* contract_var = (const contract_type*)(dev_var)->cls->contracts[(contract_enum)];                       \
  if ((contract_var) == NULL || (contract_var)->func_name == NULL) {                                                          \
    SE_FAIL(ERR_DEV_FEATURE_UNAVAILABLE, (device_id), (uint8_t)(contract_enum), SYS_DEV_FEATURE_ID(contract_type, func_name)); \
  }

/* Resolve, call the contract function and wrap a failure with the device id. */
#define SYS_DEV_DISPATCH(device_id, contract_enum, contract_type, func_name, ...)                                         \
  do {                                                                                                                    \
    SYS_DEV_RESOLVE(device_id, contract_enum, contract_type, func_name, __disp_dev, __disp_contract);                     \
    SE_TRY_WRAP(__disp_contract->func_name(__disp_dev->device_handle, ##__VA_ARGS__), ERR_DEV_DEP_FAILED,              \
                   .dev_id = (device_id));                                                                                \
    return NULL;                                                                                                          \
  } while (0)

/* ========================================================================== *
 * Integrated code blocks - Device instalation
 * ========================================================================== */

// Call once, from the `fail:` label of an install_device implementation, after
// `err` has been set to the failing step's error and before returning.
// Rolls back any partially-constructed state via `uninstall_fn`, suspending
// error reporting for the duration (teardown failures here are noise next to
// the real cause), then returns `err`. sys_device_install() adds ERR_DEV_INSTALL_FAILED
// {dev_id} on top, so the adapter doesn't wrap it again.
//
// Contract for uninstall_fn (same function used for real sys_device_uninstall()):
//  - must tolerate any subset of ctx's fields being zero/unset (calloc'd but
//    not yet populated this far into install)
//  - must NOT early-return on an individual teardown step failing; accumulate
//    the first error if you want to report one, but always free everything
#define SYS_DEV_INSTALL_FAIL(err, device_id, out_handle, uninstall_fn, ctx)                             \
  do {                                                                                                  \
    *(out_handle) = NULL;                                                                               \
    (void)(device_id);                                                                                  \
    if ((ctx) != NULL) {                                                                                \
      SE_suspend();                                                                                     \
      SE_release((uninstall_fn)((ctx)));                                                                \
      SE_resume();                                                                                      \
    }                                                                                                   \
    return (err);                                                                                       \
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

/*Run one install step; on failure wrap it as ERR_DEV_INSTALL_STEP_FAILED
  {line} (the adapter call site) and jump to the rollback label. `what` is a
  source-level label for readers only. Requires `err_h err` and a `fail:`
  label in scope.*/
#define SYS_DEV_INSTALL_STEP(expr, what)                                  \
  do {                                                                    \
    (void)(what);                                                         \
    err = (expr);                                                         \
    if (SE_IS_ERR(err)) {                                                 \
      err = SE_WRAP_ERR(err, ERR_DEV_INSTALL_STEP_FAILED, .line = __LINE__); \
      goto fail;                                                          \
    }                                                                     \
  } while (0)

/*Run one teardown step, keeping the FIRST error. Never early-returns: teardown
  must always free everything, so a failing step may not abort the rest.*/
#define SYS_DEV_TEARDOWN_STEP(err_acc, expr)                  \
  do {                                                        \
    err_h __r = (expr);                                       \
    if (SE_IS_ERR(__r) && SE_IS_OK(err_acc)) (err_acc) = __r; else SE_release(__r); \
  } while (0)

/*SYS_DEV_TEARDOWN_STEP for a driver call (esp_err_t): a failure is kept as
  ERR_DEV_DRIVER_FAILED {dev_id, line} over the ESP code, like
  SYS_DEV_CHECK_DRIVER_CALL. Also for fault-path sweeps (suspend, per-channel
  loops) that must reach every step.*/
#define SYS_DEV_TEARDOWN_DRIVER_STEP(err_acc, driver_call, ctx)                                    \
  do {                                                                                             \
    esp_err_t __drv_rc = (driver_call);                                                            \
    if (__drv_rc != ESP_OK) SYS_DEV_TEARDOWN_STEP((err_acc), SYS_DEV_DRIVER_ERR(__drv_rc, (ctx))); \
  } while (0)
