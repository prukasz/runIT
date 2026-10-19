#pragma once

#include "cmd.h"
#include "sdkconfig.h"
#include "sys_device_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Device registry (SYS_DEVICE.MD): the device types (each driver registers its own with
 * SYS_DEVICE_TYPE_DEFINE) and the instances (ID, type, struct). The registry does CREATE (size check, allocation,
 * rollback), REMOVE, INFO and OPS for every device and runs any other op from the type's op table; the driver builds
 * and releases its hardware, keeps its state and makes its outputs safe on SUSPEND. A failure is wrapped in
 * ERR_DEV_ID{id} and ERR_DEV_OP{op}.
 *
 * Op = [contract u8][op u8]: 0x00xx the device (lifecycle, below), 0x01xx-0xFExx a contract (IDs below, the one place
 * they are assigned), 0xFFxx the device's own (SYS_DEVICE_OWN_OP). A contract is ONE table X(NAME, n, args_t) that
 * gives <PREFIX>_OP_<NAME>, <prefix>_args_<NAME>_t, <PREFIX>_HANDLER(NAME) and <PREFIX>_ENTRY(NAME), so an op number
 * can't be paired with another op's struct (SYS_DEVICE_LIFECYCLE below is the pattern; sys_io.h, sys_pins.h contracts).
 *
 * Locking: one recursive lock covers the registry and every call into a device. An op must not sleep or wait for
 * anything but its own I/O, and a driver must not hold a lock of its own across sys_device_call() or
 * sys_event_publish() (SYS_DEVICE.MD "Concurrency").
 */

//#ref-defines device_id @alias Device ID @prefix SYS_DEVICE_ID_
#define SYS_DEVICE_ID_NONE 0xFFFFu  //@alias No device @description Never registered: no device in a pin reference; SUSPEND, RESUME and RESET sent to it run on every device

#define SYS_DEVICE_OP(contract, n) ((uint16_t)(((contract) << 8) | (n)))

//#ref-defines device_contract @alias Device Contract @prefix SYS_DEVICE_CONTRACT_
#define SYS_DEVICE_CONTRACT_DEVICE        0x00u  //@alias Device @description Device lifecycle operations
#define SYS_DEVICE_CONTRACT_IO            0x01u  //@alias IO pins @description IO pins operations
#define SYS_DEVICE_CONTRACT_PINS          0x02u  //@alias Pin locks @description Pin locks operations
#define SYS_DEVICE_CONTRACT_EVENT         0x03u  //@alias Events @description Events operations
#define SYS_DEVICE_CONTRACT_POWER_MONITOR 0x04u  //@alias Power monitor @description Power monitor operations
#define SYS_DEVICE_CONTRACT_POWER_VREG    0x05u  //@alias Regulator @description Voltage regulator operations
#define SYS_DEVICE_CONTRACT_POWER_MANAGER 0x06u  //@alias Power manager events @description Power manager operations
#define SYS_DEVICE_CONTRACT_POWER_USB_PD  0x07u  //@alias USB-PD sink @description USB-PD sink operations
#define SYS_DEVICE_CONTRACT_POWER_SUPPLY  0x08u  //@alias Power mux @description Power supply/mux operations
#define SYS_DEVICE_CONTRACT_HBRIDGE       0x09u  //@alias H-bridge @description H-bridge motor driver operations
#define SYS_DEVICE_CONTRACT_OWN           0xFFu  //@alias Device-specific @description Device-specific own operations

#define SYS_DEVICE_OWN_OP(n) SYS_DEVICE_OP(SYS_DEVICE_CONTRACT_OWN, n)

/** @brief Args of an op without any. */
typedef struct __attribute__((packed)) {
} sys_device_none_t;

/**
 * @brief How much a device's errors matter (SYS_DEVICE.MD "Error policy"): which severities reach the policy.
 * A device starts at LOW. Error severities themselves never change: this only filters the response.
 */
//#ref-enum @alias Device Error Importance
typedef enum {
  SYS_DEVICE_IMPORTANCE_NONE = 0,  //@alias Ignore errors @description Ignores all errors
  SYS_DEVICE_IMPORTANCE_LOW = 1,  //@alias Low @description Critical only (default)
  SYS_DEVICE_IMPORTANCE_MEDIUM = 2,  //@alias Medium @description High and critical
  SYS_DEVICE_IMPORTANCE_HIGH = 3,  //@alias High @description Medium, high and critical
  SYS_DEVICE_IMPORTANCE_CRITICAL = 4,  //@alias Critical @description Low, medium, high and critical
} sys_device_importance_e;

#define SYS_DEVICE_ERR_LEVELS 4u  // one action per severity LOW, MEDIUM, HIGH, CRITICAL

/** @brief SET_ERROR_POLICY args and GET_ERROR_POLICY answer. */
typedef struct __attribute__((packed)) {
  uint8_t importance;                       /**< sys_device_importance_e */
  uint32_t actions[SYS_DEVICE_ERR_LEVELS];  /**< Record run for low, medium, high, critical (0: none) */
} sys_device_error_policy_t;

/**
 * @brief Writes a contract op's cmd_f: parses the payload (exactly
 * sizeof(args_t) bytes, else ERR_CMD_BAD_LEN) and runs the body that follows.
 * The body sees ctx (the device's struct), a (const args_t*), out, out_cap,
 * out_len. `in` is the payload only: the registry strips the op.
 */
#define SYS_DEVICE_HANDLER(fn, args_t)                                                                            \
  static err_h fn##_body(void* ctx, const args_t* args, uint8_t* out, uint32_t out_cap, uint32_t* out_len);       \
  static err_h fn(void* ctx, const uint8_t* in, uint32_t in_len, uint8_t* out, uint32_t out_cap,                  \
                  uint32_t* out_len) {                                                                            \
    args_t args;                                                                                                  \
    ERR_CHECK(in_len == sizeof(args), ERR_CMD_BAD_LEN, .got = (uint16_t)in_len, .need = (uint16_t)sizeof(args)); \
    memcpy(&args, in, sizeof(args));                                                                              \
    return fn##_body(ctx, &args, out, out_cap, out_len);                                                          \
  }                                                                                                               \
  static err_h fn##_body(void* ctx, const args_t* args, uint8_t* out, uint32_t out_cap, uint32_t* out_len)

/**
 * @brief Device contract (0x00xx): the lifecycle. CREATE (0x00) is not in the
 * table: it has no instance yet. REMOVE, INFO and OPS are answered by the
 * registry for every device: a driver's op table lists none of them.
 */
#define SYS_DEVICE_LIFECYCLE(X) \
  X(RESET, 0x01, sys_device_none_t) \
  X(SUSPEND, 0x02, sys_device_none_t) \
  X(RESUME, 0x03, sys_device_none_t) \
  X(REMOVE, 0x04, sys_device_none_t) \
  X(INFO, 0x05, sys_device_none_t) \
  X(OPS, 0x06, sys_device_none_t) \
  X(SET_ERROR_POLICY, 0x07, sys_device_error_policy_t) \
  X(GET_ERROR_POLICY, 0x08, sys_device_none_t)

#define SYS_DEVICE__ID(name, n, args_t) SYS_DEVICE_OP_##name = SYS_DEVICE_OP(SYS_DEVICE_CONTRACT_DEVICE, n),
#define SYS_DEVICE__ARGS(name, n, args_t) typedef args_t sys_device_args_##name##_t;

enum { SYS_DEVICE_OP_CREATE = SYS_DEVICE_OP(SYS_DEVICE_CONTRACT_DEVICE, 0x00), SYS_DEVICE_LIFECYCLE(SYS_DEVICE__ID) };  // CREATE: [type u16][create struct]
SYS_DEVICE_LIFECYCLE(SYS_DEVICE__ARGS)

/* Events of the device contract (sys_event.h "Events tables"): a device's `suspended` flag changed. Source = the device,
 * or SYS_DEVICE_ID_NONE for a sweep (SUSPEND / RESUME of every device: one event, not one per device). NOTIFY: the app
 * learns of a safe state or a fault response without asking. The IDs are made in sys_event.h (DEVICE_EV_*), which has the
 * flag values; this layer only reports the change (sys_device_set_state_listener). */
#define SYS_DEVICE_EVENTS(X) \
  X(SUSPENDED, 0x00, SYS_EVENT_NOTIFY) \
  X(RESUMED, 0x01, SYS_EVENT_NOTIFY)

#define DEVICE_HANDLER(name) SYS_DEVICE_HANDLER(sys_device_h_##name, sys_device_args_##name##_t)
#define DEVICE_ENTRY(name) {SYS_DEVICE_OP_##name, sys_device_h_##name}

/** @brief INFO answer. */
typedef struct __attribute__((packed)) {
  uint16_t type_id;   /**< Device type ID */
  uint8_t suspended;  /**< 1 if suspended, 0 otherwise */
} sys_device_info_t;

/* ----------------------------------------------------------------------------
 * Errors (module ERR_MOD_SYS_DEVICE). Besides the registry's, the ones every
 * device raises the same way (suspended, range, pin), so they look the same
 * on all.
 * ------------------------------------------------------------------------- */

#define ERR_SYS_DEVICE_MAP(X)                                                                                     \
  X(ERR_DEV_BAD_ARG,        ERR_TAG(ERR_MOD_SYS_DEVICE, 1),  ERR_SEV_HIGH,   {})                                  \
  X(ERR_DEV_BAD_ID,         ERR_TAG(ERR_MOD_SYS_DEVICE, 2),  ERR_SEV_HIGH,   { uint16_t id; }) /* SYS_DEVICE_ID_NONE */ \
  X(ERR_DEV_NOT_FOUND,      ERR_TAG(ERR_MOD_SYS_DEVICE, 3),  ERR_SEV_MEDIUM, { uint16_t id; })                    \
  X(ERR_DEV_ID_TAKEN,       ERR_TAG(ERR_MOD_SYS_DEVICE, 4),  ERR_SEV_MEDIUM, { uint16_t id; })                    \
  X(ERR_DEV_TABLE_FULL,     ERR_TAG(ERR_MOD_SYS_DEVICE, 5),  ERR_SEV_HIGH,   { uint16_t id; }) /* CONFIG_SYS_DEVICE_MAX */ \
  X(ERR_DEV_TYPE_UNKNOWN,   ERR_TAG(ERR_MOD_SYS_DEVICE, 6),  ERR_SEV_HIGH,   { uint16_t type; })                   \
  X(ERR_DEV_SUSPENDED,      ERR_TAG(ERR_MOD_SYS_DEVICE, 10), ERR_SEV_MEDIUM, { uint16_t id; })                    \
  X(ERR_DEV_NO_MEM,         ERR_TAG(ERR_MOD_SYS_DEVICE, 11), ERR_SEV_CRITICAL, { uint16_t id; uint16_t size; })   \
  X(ERR_DEV_INSTALL,        ERR_TAG(ERR_MOD_SYS_DEVICE, 12), ERR_SEV_NONE,   { uint16_t id; uint16_t type; }) /* context: create failed */ \
  X(ERR_DEV_ID,             ERR_TAG(ERR_MOD_SYS_DEVICE, 13), ERR_SEV_NONE,   { uint16_t id; }) /* context: route step / sweep */ \
  X(ERR_DEV_BAD_PIN,        ERR_TAG(ERR_MOD_SYS_DEVICE, 15), ERR_SEV_HIGH,   { uint16_t id; uint8_t pin; })       \
  X(ERR_DEV_RANGE,          ERR_TAG(ERR_MOD_SYS_DEVICE, 16), ERR_SEV_HIGH,   { uint16_t id; int32_t value; int32_t min; int32_t max; }) /* a value the chip would reject (signed: thresholds may be negative) */ \
  X(ERR_DEV_OP,             ERR_TAG(ERR_MOD_SYS_DEVICE, 17), ERR_SEV_NONE,   { uint16_t op; }) /* context: route step */ \
  X(ERR_DEV_UNKNOWN_OP,     ERR_TAG(ERR_MOD_SYS_DEVICE, 18), ERR_SEV_HIGH,   { uint16_t op; }) /* an op (contract) the device doesn't have */ \
  X(ERR_DEV_PIN_LOCKED,     ERR_TAG(ERR_MOD_SYS_DEVICE, 19), ERR_SEV_HIGH,   { uint16_t id; uint8_t pin; }) /* a locked pin: only its owner drives it (sys_pins.h) */ \
  X(ERR_DEV_IN_USE,         ERR_TAG(ERR_MOD_SYS_DEVICE, 20), ERR_SEV_MEDIUM, { uint16_t id; uint16_t user; }) /* REMOVE of a device another one uses */ \
  X(ERR_DEV_DEPS_FULL,      ERR_TAG(ERR_MOD_SYS_DEVICE, 21), ERR_SEV_HIGH,   { uint16_t id; }) /* CONFIG_SYS_DEVICE_MAX_DEPS */ \
  X(ERR_DEV_FAULT_RESPONSE, ERR_TAG(ERR_MOD_SYS_DEVICE, 22), ERR_SEV_HIGH,   { uint16_t id; uint8_t sev; uint8_t stage; uint32_t action; }) /* the policy's response to an error of device `id` failed (stage: sys_device_fault_stage_e); diagnostics only */

ERR_TAGS_DECLARE(ERR_SYS_DEVICE_MAP)

/* Widened to int64 in a function: at the call the compiler would see a u8's range and warn (-Wtype-limits, u8 >= 0). */
static inline bool dev_in_range(int64_t v, int64_t lo, int64_t hi) {
  return v >= lo && v <= hi;
}

/**
 * @brief ERR_DEV_RANGE unless lo <= v <= hi. The bounds are written once: the check and the error's min / max come from
 * the same expressions. The value and the bounds are reported as int32_t (ERR_DEV_RANGE is signed).
 * The parameter is `dev`, not `id`: a parameter named `id` would also be replaced after the `.` of `.id =`.
 * @usage SYS_DEV_CHECK_IN_RANGE(c->base.id, a->duty, 0, IO_PWM_DUTY_MAX);
 */
#define SYS_DEV_CHECK_IN_RANGE(dev, v, lo, hi) \
  ERR_CHECK(dev_in_range((v), (lo), (hi)), ERR_DEV_RANGE, .id = (dev), .value = (int32_t)(v), .min = (int32_t)(lo), \
            .max = (int32_t)(hi))

/* ----------------------------------------------------------------------------
 * Device types (drivers)
 * ------------------------------------------------------------------------- */

/**
 * @brief First member of every device's struct: what the registry and the
 * shared checks read. The registry fills `id` before create().
 */
typedef struct {
  uint16_t id;
  bool suspended;     // fault safe state: writes refused (sys_device_check_active) until RESUME
  uint8_t dep_count;  // sys_device_use()
  uint16_t deps[CONFIG_SYS_DEVICE_MAX_DEPS];  // the devices this one uses, each once
} sys_device_base_t;

/**
 * @brief One entry of a device's op table.
 */
typedef struct {
  uint16_t op;
  cmd_f fn;  // gets the device's struct as ctx and the op's args only
} sys_device_op_t;

/**
 * @brief A device type: everything the registry needs to make, run and remove
 * its instances. Defined once per driver with SYS_DEVICE_TYPE_DEFINE.
 */
typedef struct {
  uint16_t type;      // SYS_DEVICE_TYPE_* (sys_device_types.h)
  uint16_t cfg_size;  // create struct, exactly (ERR_CMD_BAD_LEN otherwise)
  uint16_t ctx_size;  // the device's struct, sys_device_base_t first; allocated zeroed by the registry
  /** Builds the device: checks the cfg, fills ctx, sets up the hardware. On a failure the registry calls
   *  uninstall() and frees ctx. `cfg` is the packed create struct (cfg_size bytes; may be unaligned). */
  err_h (*create)(void* ctx, const void* cfg);
  /** Releases what create() built: REMOVE and the create rollback. Every step runs, guarded by what was
   *  built; the registry frees ctx afterwards. */
  err_h (*uninstall)(void* ctx);
  /** SUSPEND: the safe state (outputs, pins). Called with `suspended` as it was before; the registry sets the flag
   *  afterwards, even when this fails (a half-safe device refuses writes). */
  err_h (*suspend)(void* ctx);
  /** RESUME: back to work. Called only while suspended; the registry clears the flag when it succeeds. */
  err_h (*resume)(void* ctx);
  const sys_device_op_t* ops;  // the type's own ops (RESET is one); never REMOVE / INFO / OPS / SUSPEND / RESUME
  uint16_t op_count;
} sys_device_type_t;

#define SYS_DEVICE_OPS(table) .ops = (table), .op_count = (uint16_t)ARRAY_LEN(table)

/** @brief Adds a type. Called by SYS_DEVICE_TYPE_DEFINE before app_main; asserts on a misdefined type. */
void sys_device_type_register(const sys_device_type_t* type);

/**
 * @brief Defines the driver's type and registers it before app_main. The
 * driver's component must be linked as WHOLE_ARCHIVE: nothing references it,
 * and the linker would drop the registration.
 * @usage SYS_DEVICE_TYPE_DEFINE(.type = SYS_DEVICE_TYPE_PCA9685, .cfg_size = sizeof(pca9685_cfg_t),
 *                               .ctx_size = sizeof(pca_t), .create = create, .uninstall = uninstall,
 *                               .suspend = suspend, .resume = resume, SYS_DEVICE_OPS(k_ops));
 */
#define SYS_DEVICE_TYPE_DEFINE(...)                                                \
  static const sys_device_type_t s_sys_device_type = {__VA_ARGS__};                \
  __attribute__((constructor)) static void s_sys_device_type_register(void) {      \
    sys_device_type_register(&s_sys_device_type);                                  \
  }

/** @brief Before a write: ERR_DEV_SUSPENDED while the device is suspended. */
static inline MUST_USE err_h sys_device_check_active(const sys_device_base_t* dev) {
  ERR_CHECK(!dev->suspended, ERR_DEV_SUSPENDED, .id = dev->id);
  return NULL;
}

/* ----------------------------------------------------------------------------
 * Calls
 * ------------------------------------------------------------------------- */

/**
 * @brief Runs op `op` of device `id` without a transport: device-to-device
 * calls, the contract wrappers, event listeners, timers, the board creating
 * its devices (op CREATE, args [type u16][create struct]). `args` and `out` are
 * the op's wire payload and answer (any size: the args are used in place);
 * `out_len` may be NULL.
 */
MUST_USE err_h sys_device_call(uint16_t id, uint16_t op, const void* args, uint32_t args_len, void* out,
                               uint32_t out_cap, uint32_t* out_len);

/**
 * @brief A contract's guard (SYS_DEVICE.MD "Guards"): sees every sys_device_call of an op of its contract before the
 * device does, remote commands included. It sets `*handled` and returns the result to take over the call (it runs
 * the op itself with sys_device_call_direct, or refuses it), or leaves `*handled` false and returns NULL to pass the
 * call on. Runs without the registry lock; it must not be reached from inside a device op (lock order: the guard's
 * own lock, then the registry's).
 */
typedef err_h (*sys_device_guard_f)(uint16_t id, uint16_t op, const uint8_t* args, uint32_t args_len, bool* handled);

/** @brief Sets the guard of `contract`. Boot only; one per contract, up to CONFIG_SYS_DEVICE_GUARDS. */
MUST_USE err_h sys_device_set_guard(uint8_t contract, sys_device_guard_f fn);

/** @brief sys_device_call without the guard: for the guard's owner, to run the op it decided on. */
MUST_USE err_h sys_device_call_direct(uint16_t id, uint16_t op, const void* args, uint32_t args_len, void* out,
                                      uint32_t out_cap, uint32_t* out_len);

/**
 * @brief Runs SUSPEND, RESUME, RESET or REMOVE (`op`, else ERR_DEV_UNKNOWN_OP) on every instance: newest first (a
 * device before the devices it uses), RESUME oldest first; REMOVE leaves the board's own devices
 * (sys_device_mark_system). SUSPEND of every device is the fault safe state. Best effort: all are tried, the first
 * error is returned (wrapped in ERR_DEV_ID), the others go to err_report().
 * @usage err_report(sys_device_sweep(SYS_DEVICE_OP_SUSPEND));
 */
MUST_USE err_h sys_device_sweep(uint16_t op);

/**
 * @brief Marks every device registered now as the board's own (created by the system at boot): the REMOVE sweep
 * leaves them, so changing the project removes only the devices it created. An explicit REMOVE of one still works.
 * Boot step, after the onboard devices are created.
 */
void sys_device_mark_system(void);

/**
 * @brief Declares that device `dev` uses device `dep` (one of its pins, a bus
 * device...). Called by the driver's create() for every device it will call,
 * before using it. The registry then refuses to REMOVE `dep` while `dev` exists
 * (ERR_DEV_IN_USE) and sweeps in an order where users come first. Any IDs:
 * the order is the creation order (a device can only use one that exists).
 * Declaring the same dep twice is fine.
 * @return ERR_DEV_NOT_FOUND{dep} unless it exists (and isn't `dev`),
 *         ERR_DEV_DEPS_FULL past CONFIG_SYS_DEVICE_MAX_DEPS.
 * @usage if (sys_io_pin_valid(c->oe)) { ERR_TRY(sys_device_use(&c->base, c->oe.device)); }
 */
MUST_USE err_h sys_device_use(sys_device_base_t* dev, uint16_t dep);

/* ----------------------------------------------------------------------------
 * Error policy (SYS_DEVICE.MD "Error policy"): per instance, importance and an
 * action per severity, set with SET_ERROR_POLICY. The registry finds the
 * devices of a reported chain, filters them and responds: CRITICAL suspends
 * every device, then the device's action for that severity runs (through the
 * runner the app sets, so sys_device doesn't know sys_actions).
 * ------------------------------------------------------------------------- */

/** @brief Stage of a response, in ERR_DEV_FAULT_RESPONSE. */
//#ref-enum @alias Fault Response Stage
typedef enum {
  SYS_DEVICE_FAULT_STAGE_VM_STOP = 0,  //@alias Vm stop @description Stop the VM
  SYS_DEVICE_FAULT_STAGE_SUSPEND = 1,  //@alias Suspend @description Suspend all devices
  SYS_DEVICE_FAULT_STAGE_ACTION = 2,  //@alias Action @description Run configured action
} sys_device_fault_stage_e;

/**
 * @brief Runs action `action` (a sys_actions ID, u32) as a response; `chain` is the reported chain (borrowed), so the runner can
 * refuse an action whose own failure is in it. The app sets it (runit: sys_actions_request, queued on the router).
 * @return A failure to start it, or NULL.
 */
typedef err_h (*sys_device_action_runner_f)(uint32_t action, err_h chain);

/**
 * @brief Stops the VM on a critical error (stage SYS_DEVICE_FAULT_STAGE_VM_STOP): runs before the devices are
 * suspended, so the program stops writing to them. `device_id` is the failing device or SYS_DEVICE_ID_NONE. The app
 * sets it (runit: vm_stop_on_fault); without one the VM is left to the safe state alone.
 */
typedef err_h (*sys_device_vm_stop_f)(uint16_t device_id);

/**
 * @brief Told when a device's `suspended` flag changes (SUSPEND, RESUME, and the safe state): `id` is the device, or
 * SYS_DEVICE_ID_NONE after a sweep that changed any device (one call for the whole sweep). Called under the registry lock,
 * in the task of the op: it must not wait. The app sets it (runit: the DEVICE events of sys_event).
 */
typedef void (*sys_device_state_f)(uint16_t id, bool suspended);

/** @brief Sets the state listener. Boot only. */
void sys_device_set_state_listener(sys_device_state_f fn);

/** @brief Sets the VM stop. Boot only. */
void sys_device_set_vm_stop(sys_device_vm_stop_f fn);

/** @brief Sets the action runner. Boot only. Without one, actions are not run (CRITICAL still suspends). */
void sys_device_set_action_runner(sys_device_action_runner_f fn);

/**
 * @brief Runs the policy on a reported chain (the app's error sink calls it). Walks from the outermost node: each
 * device (ERR_DEV_ID) once, at its first mention, with the severity from there down; a device at importance NONE
 * ends the walk (it and every deeper cause are ignored). Admitted: CRITICAL at any importance but NONE, else when
 * severity + importance > CRITICAL. An admitted error: CRITICAL suspends every device, then the device's action for
 * that severity runs (a device that no longer exists, a failed CREATE, gets the suspend only, for a critical error).
 * A critical node before any device suspends every device too. A chain holding
 * ERR_DEV_FAULT_RESPONSE, or one reported while a response runs on this task, is not handled (no loops).
 * @return The first response failure (owned; the others are freed), or NULL. Does not free `chain`.
 */
MUST_USE err_h sys_device_handle_error(err_h chain);

/** @brief The policy of device `id`, false if it isn't registered. Doesn't take the registry lock (a short critical
 *  section), so the error path can't wait on an op in progress. */
bool sys_device_error_policy_get(uint16_t id, sys_device_error_policy_t* out);

#ifdef __cplusplus
}
#endif
