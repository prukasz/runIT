#pragma once

#include "cmd.h"
#include "sdkconfig.h"
#include "sys_device_errors.h"
#include "sys_device_policy.h"
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
 * they are assigned), 0xFFxx the device's own (SYS_DEVICE_OWN_OP). An op has a read side, a write side or both
 * (cmd_dir_e): the read takes the key (a pin, a channel; nothing) and answers what the write takes (the mirror, CMD.MD).
 * A contract is ONE table P_OPERATIONS of X(P, NAME, n, key_t, value_t); SYS_DEVICE_CONTRACT_DECLARE(P, contract ID)
 * gives P_CONTRACT, P_OP_<NAME>, P_key_<NAME>_t, P_value_<NAME>_t, and OP_READ(P, NAME) / OP_WRITE(P, NAME) (the
 * handlers) and OP_ENTRY(P, NAME) (both sides), OP_ENTRY_R, OP_ENTRY_W (one) use them, so an op number can't be paired
 * with another op's struct (SYS_DEVICE_OPERATIONS below is the pattern; sys_io.h, sys_pins.h contracts).
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

/** @brief INFO answer. */
typedef struct __attribute__((packed)) {
  uint16_t type_id;   /**< Device type ID */
  uint8_t suspended;  /**< 1 if suspended, 0 otherwise */
} sys_device_info_t;

/**
 * @brief Device contract (0x00xx, prefix SYS_DEVICE): the lifecycle, X(P, NAME, n, key_t, value_t). CREATE (0x00,
 * write) is not in the table: it has no instance yet. Writes: RESET, SUSPEND, RESUME, REMOVE; reads: INFO, OPS
 * (answered as a list, so its value is none here); both: ERROR_POLICY. SUSPEND, RESUME, REMOVE, INFO, OPS and
 * ERROR_POLICY are the registry's for every device: a driver's op table lists none of them.
 */
#define SYS_DEVICE_OPERATIONS(X, P)                                      \
  X(P, RESET,        0x01, sys_device_none_t, sys_device_none_t)         \
  X(P, SUSPEND,      0x02, sys_device_none_t, sys_device_none_t)         \
  X(P, RESUME,       0x03, sys_device_none_t, sys_device_none_t)         \
  X(P, REMOVE,       0x04, sys_device_none_t, sys_device_none_t)         \
  X(P, INFO,         0x05, sys_device_none_t, sys_device_info_t)         \
  X(P, OPS,          0x06, sys_device_none_t, sys_device_none_t)         \
  X(P, ERROR_POLICY, 0x07, sys_device_none_t, sys_device_error_policy_t)

/* The contract tables' expansion, shared by every contract: SYS_DEVICE_CONTRACT_DECLARE(P, contract ID) on the table
 * P_OPERATIONS(X, P) gives P_CONTRACT (the ID), P_OP_<NAME> and the types P_key_<NAME>_t, P_value_<NAME>_t. */
#define SYS_DEVICE__ID(P, name, n, key_t, value_t) P##_OP_##name = SYS_DEVICE_OP(P##_CONTRACT, n),
#define SYS_DEVICE__TYPES(P, name, n, key_t, value_t) \
  typedef key_t P##_key_##name##_t;                   \
  typedef value_t P##_value_##name##_t;
#define SYS_DEVICE_CONTRACT_DECLARE(P, contract)                         \
  enum { P##_CONTRACT = (contract), P##_OPERATIONS(SYS_DEVICE__ID, P) }; \
  P##_OPERATIONS(SYS_DEVICE__TYPES, P)

enum { SYS_DEVICE_OP_CREATE = SYS_DEVICE_OP(SYS_DEVICE_CONTRACT_DEVICE, 0x00) };  // CREATE: [type u16][create struct]
SYS_DEVICE_CONTRACT_DECLARE(SYS_DEVICE, SYS_DEVICE_CONTRACT_DEVICE)

/* A contract's events table P_EVENTS of rows X(P, NAME, n, flags) (sys_event.h "Events tables") gives
 * P_EV_<NAME> = (P_CONTRACT << 8) | n and P_EV_<NAME>_FLAGS. Here, below sys_event, so every contract can use it. */
#define SYS_EVENT__ID(P, name, n, flags) \
  P##_EV_##name = SYS_DEVICE_OP(P##_CONTRACT, n), P##_EV_##name##_FLAGS = (flags),
#define SYS_EVENTS_DECLARE(P) enum { P##_EVENTS(SYS_EVENT__ID, P) };

/* Events of the device contract (sys_event.h "Events tables"): a device's `suspended` flag changed. Source = the device,
 * or SYS_DEVICE_ID_NONE for a sweep (SUSPEND / RESUME of every device: one event, not one per device). NOTIFY: the app
 * learns of a safe state or a fault response without asking. The IDs are made in sys_event.h (SYS_DEVICE_EV_*), which
 * has the flag values; this layer only reports the change (sys_device_set_state_listener). */
#define SYS_DEVICE_EVENTS(X, P)           \
  X(P, SUSPENDED, 0x00, SYS_EVENT_NOTIFY) \
  X(P, RESUMED,   0x01, SYS_EVENT_NOTIFY)

/* The handlers and op-table rows of op NAME of the contract with prefix P (OP_WRITE(SYS_DEVICE, RESET),
 * OP_ENTRY_R(PD, LIMITS)): OP_READ / OP_WRITE write the side's cmd_f (the read parses the key, the write the value),
 * OP_ENTRY lists both sides, OP_ENTRY_R / OP_ENTRY_W one. */
#define OP_READ(P, name)    SYS_DEVICE_HANDLER(P##_r_##name, P##_key_##name##_t)
#define OP_WRITE(P, name)   SYS_DEVICE_HANDLER(P##_w_##name, P##_value_##name##_t)
#define OP_ENTRY(P, name)   {P##_OP_##name, P##_r_##name, P##_w_##name}
#define OP_ENTRY_R(P, name) {P##_OP_##name, P##_r_##name, NULL}
#define OP_ENTRY_W(P, name) {P##_OP_##name, NULL, P##_w_##name}

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
 * @brief One entry of a device's op table: the op and its two sides (NULL = the device has no such side). Each gets
 * the device's struct as ctx and the side's args only.
 */
typedef struct {
  uint16_t op;
  cmd_f read;
  cmd_f write;
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
  /** Optional: false for an op of the table this device can't run as wired (the DRV8962's POWER_MONITOR without its
   *  IPROPI pins): left out of OPS and answered ERR_DEV_UNKNOWN_OP. NULL: every op of the table. */
  bool (*has_op)(const void* ctx, uint16_t op);
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
 * @brief Runs side `dir` (cmd_dir_e) of op `op` of device `id` without a transport: device-to-device
 * calls, the contract wrappers, event listeners, timers, the board creating
 * its devices (op CREATE, a write, args [type u16][create struct]). `args` and `out` are
 * the op's wire payload and answer (any size: the args are used in place);
 * `out_len` may be NULL.
 */
MUST_USE err_h sys_device_call(uint16_t id, uint8_t dir, uint16_t op, const void* args, uint32_t args_len, void* out,
                               uint32_t out_cap, uint32_t* out_len);

/** @brief The write side of `op`: sys_device_call with no answer. The contract wrappers' setters and actions. */
MUST_USE err_h sys_device_write(uint16_t id, uint16_t op, const void* args, uint32_t args_len);

/**
 * @brief The read side of `op` with the key `args`, whose answer has a fixed size (a reading, a mask): ERR_CMD_BAD_LEN
 * unless the device answered exactly `out_size` bytes. The contract wrappers' getters.
 */
MUST_USE err_h sys_device_read(uint16_t id, uint16_t op, const void* args, uint32_t args_len, void* out,
                               uint32_t out_size);

/**
 * @brief A contract's guard (SYS_DEVICE.MD "Guards"): sees every sys_device_call of an op of its contract before the
 * device does, remote commands included. It sets `*handled` and returns the result to take over the call (it runs
 * the op itself with sys_device_call_direct, or refuses it), or leaves `*handled` false and returns NULL to pass the
 * call on. Runs without the registry lock; it must not be reached from inside a device op (lock order: the guard's
 * own lock, then the registry's).
 */
typedef err_h (*sys_device_guard_f)(uint16_t id, uint8_t dir, uint16_t op, const uint8_t* args, uint32_t args_len,
                                    bool* handled);

/** @brief Sets the guard of `contract`. Boot only; one per contract, up to CONFIG_SYS_DEVICE_GUARDS. */
MUST_USE err_h sys_device_set_guard(uint8_t contract, sys_device_guard_f fn);

/** @brief sys_device_call without the guard: for the guard's owner, to run the op it decided on. */
MUST_USE err_h sys_device_call_direct(uint16_t id, uint8_t dir, uint16_t op, const void* args, uint32_t args_len,
                                      void* out, uint32_t out_cap, uint32_t* out_len);

/**
 * @brief Runs SUSPEND, RESUME, RESET or REMOVE (`op`, else ERR_DEV_UNKNOWN_OP) on every instance: newest first (a
 * device before the devices it uses), RESUME oldest first; REMOVE leaves the board's own devices
 * (sys_device_mark_system). SUSPEND of every device is the fault safe state. Best effort: all are tried, the first
 * error is returned (wrapped in ERR_DEV_ID), the others go to err_report().
 * @usage err_report(sys_device_sweep(SYS_DEVICE_OP_SUSPEND));
 */
MUST_USE err_h sys_device_sweep(uint16_t op);

/**
 * @brief Marks every device registered now as the board's own (created by the system at boot): they can't be removed
 * (ERR_DEV_SYSTEM) and the REMOVE sweep leaves them, so changing the project removes only the devices it created.
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
 * State listener
 * ------------------------------------------------------------------------- */

/**
 * @brief Told when a device's `suspended` flag changes (SUSPEND, RESUME, and the safe state): `id` is the device, or
 * SYS_DEVICE_ID_NONE after a sweep that changed any device (one call for the whole sweep). Called under the registry
 * lock, in the task of the op: it must not wait. The app sets it (runit: the SYS_DEVICE events of sys_event).
 */
typedef void (*sys_device_state_f)(uint16_t id, bool suspended);

/** @brief Sets the state listener. Boot only. */
void sys_device_set_state_listener(sys_device_state_f fn);


#ifdef __cplusplus
}
#endif
