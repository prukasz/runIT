#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
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

#define SYS_DEVICE_ID_NONE 0xFFFFu  // "no device" in pin references; never registered

#define SYS_DEVICE_OP(contract, n) ((uint16_t)(((contract) << 8) | (n)))

#define SYS_DEVICE_CONTRACT_DEVICE 0x00u  // CREATE and the lifecycle, below
#define SYS_DEVICE_CONTRACT_IO 0x01u      // sys_io.h
#define SYS_DEVICE_CONTRACT_PINS 0x02u    // sys_pins.h
#define SYS_DEVICE_CONTRACT_EVENT 0x03u   // sys_event.h: ON_EVENT, a device listening to events
#define SYS_DEVICE_CONTRACT_POWER_MONITOR 0x04u  // sys_power_monitor.h: voltage / current / power readings, current alerts
#define SYS_DEVICE_CONTRACT_POWER_VREG 0x05u     // sys_power_vreg.h: an adjustable regulator (enable, voltage, current limit)
#define SYS_DEVICE_CONTRACT_OWN 0xFFu     // the device's own ops

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
#define SYS_DEVICE_HANDLER(fn, args_t)                                                            static err_h fn##_body(void* ctx, const args_t* a, uint8_t* out, uint32_t out_cap, uint32_t* out_len);   static err_h fn(void* ctx, const uint8_t* in, uint32_t in_len, uint8_t* out, uint32_t out_cap,                   uint32_t* out_len) {                                                              args_t a;                                                                                       ERR_CHECK(in_len == sizeof(a), ERR_CMD_BAD_LEN, .got = (uint16_t)in_len, .need = (uint16_t)sizeof(a));     memcpy(&a, in, sizeof(a));                                                                      return fn##_body(ctx, &a, out, out_cap, out_len);                                             }                                                                                               static err_h fn##_body(void* ctx, const args_t* a, uint8_t* out, uint32_t out_cap, uint32_t* out_len)

/**
 * @brief Device contract (0x00xx): the lifecycle. CREATE (0x00) is not in the
 * table: it has no instance yet. REMOVE, INFO and OPS are answered by the
 * registry for every device: a driver's op table lists none of them.
 */
// clang-format off
#define SYS_DEVICE_LIFECYCLE(X)                  X(RESET,   0x01, sys_device_none_t)  /* outputs off, defaults */   X(SUSPEND, 0x02, sys_device_none_t)  /* required: outputs safe; then refuses writes until RESUME */   X(RESUME,  0x03, sys_device_none_t)  /* required */   X(REMOVE,  0x04, sys_device_none_t)  /* required: releases the hardware, sys_device_remove_me, frees the struct */   X(INFO,    0x05, sys_device_none_t)  /* -> sys_device_info_t */   X(OPS,     0x06, sys_device_none_t)  /* -> u16[] every op the device answers (generic: sys_device_dispatch) */
// clang-format on

#define SYS_DEVICE__ID(name, n, args_t) SYS_DEVICE_OP_##name = SYS_DEVICE_OP(SYS_DEVICE_CONTRACT_DEVICE, n),
#define SYS_DEVICE__ARGS(name, n, args_t) typedef args_t sys_device_args_##name##_t;

enum { SYS_DEVICE_OP_CREATE = SYS_DEVICE_OP(SYS_DEVICE_CONTRACT_DEVICE, 0x00), SYS_DEVICE_LIFECYCLE(SYS_DEVICE__ID) };  // CREATE: [type u16][create struct]
SYS_DEVICE_LIFECYCLE(SYS_DEVICE__ARGS)

#define DEVICE_HANDLER(name) SYS_DEVICE_HANDLER(sys_device_h_##name, sys_device_args_##name##_t)
#define DEVICE_ENTRY(name) {SYS_DEVICE_OP_##name, sys_device_h_##name}

/** @brief INFO answer. */
typedef struct __attribute__((packed)) {
  uint16_t type_id;
  uint8_t suspended;
} sys_device_info_t;

/* ----------------------------------------------------------------------------
 * Errors (module ERR_MOD_SYS_DEVICE). Besides the registry's, the ones every
 * device raises the same way (suspended, range, pin), so they look the same
 * on all.
 * ------------------------------------------------------------------------- */

// clang-format off
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
  X(ERR_DEV_DEPS_FULL,      ERR_TAG(ERR_MOD_SYS_DEVICE, 21), ERR_SEV_HIGH,   { uint16_t id; }) /* CONFIG_SYS_DEVICE_MAX_DEPS */
// clang-format on

ERR_TAGS_DECLARE(ERR_SYS_DEVICE_MAP)

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
  const sys_device_op_t* ops;  // must answer SUSPEND and RESUME; never REMOVE / INFO / OPS
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
 *                               .ctx_size = sizeof(pca_t), .create = create, .uninstall = uninstall, SYS_DEVICE_OPS(k_ops));
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
 * @brief Fault safe state: SUSPEND to every instance, newest first (a device
 * before the devices it uses). Best effort: all are tried, the first error is
 * returned (wrapped in ERR_DEV_ID), the others go to err_report().
 */
MUST_USE err_h sys_device_suspend_all(void);
MUST_USE err_h sys_device_resume_all(void);  // oldest first: the used devices before their users
MUST_USE err_h sys_device_remove_all(void);  // newest first

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

/**
 * @brief Keeps the first error of a sequence whose steps all run (teardown).
 * @usage err_h err = NULL; SYS_DEVICE_KEEP_FIRST(err, chip_sleep(c, true)); SYS_DEVICE_KEEP_FIRST(err, ...);
 */
#define SYS_DEVICE_KEEP_FIRST(acc, call)  \
  do {                                    \
    err_h _keep_ = (call);                \
    if (_keep_ != NULL) {                 \
      if ((acc) == NULL) {                \
        (acc) = _keep_;                   \
      } else {                            \
        err_free(_keep_);                 \
      }                                   \
    }                                     \
  } while (0)

#ifdef __cplusplus
}
#endif
