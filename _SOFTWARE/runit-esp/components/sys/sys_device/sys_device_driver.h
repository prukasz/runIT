#pragma once

#include "sys_device.h"
#include "sys_event.h"  // SYS_DEVICE_OWN_EVENTS

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief What a driver needs to be a device (SYS_DEVICE.MD "A driver"): its type, its op table and the handler macros.
 * The registry allocates the driver's struct zeroed, calls create() with the device's ID, and keeps nothing of the
 * driver in it: the driver stores its own ID if it needs it. The calls other code makes are in sys_device.h.
 *
 * A driver is one source, written in this order:
 *   SYS_DEVICE_TYPE_DECLARE(PCA9685, 0x0041)     // its type ID, once: PCA9685_TYPE
 *   typedef struct { ... } pca_t;                // its struct
 *   #define SYS_DEVICE_CTX pca_t                 // every handler's body sees it as `c`
 *   OP_DECLARE_WRITE(CONTRACT_IO, OP_LEVEL) { ... }  // the handlers: c, args, out
 *   static err_h create(uint8_t id, pca_t* c, const pca9685_cfg_t* cfg) { ... }
 *   static const sys_device_op_t k_ops[] = {OP_ENTRY(CONTRACT_IO, OP_LEVEL), ...};
 *   SYS_DEVICE_TYPE_DEFINE(PCA9685, pca9685_cfg_t, create, suspend, resume, k_ops)
 */

/* The struct a handler's ctx is: a driver names its own once, `#define SYS_DEVICE_CTX pca_t`, after the struct and
 * before the first handler. Every handler's body and the type's hooks then take it typed, so no body casts ctx. A
 * source that writes handlers and has not named it does not compile (SYS_DEVICE_CTX is not a type). */

/* Writes a cmd_f of an op (CMD_HANDLER, cmd_table.h): parses the payload (exactly sizeof(args_t) bytes, else
 * ERR_CMD_BAD_LEN) and runs the body that follows. The body sees `c` (the driver's struct, SYS_DEVICE_CTX*), `args`
 * (const args_t*) and `out`, and need not use any of them. `in` is the payload only: the registry strips the op. A
 * driver writes OP_DECLARE_READ / OP_DECLARE_WRITE. */
#define SYS_DEVICE__HANDLER(fn, args_t) CMD_HANDLER(fn, SYS_DEVICE_CTX, args_t)

/* The handlers and op-table rows of op OP_NAME of the table with prefix P, a contract's or the device's own
 * (OP_DECLARE_WRITE(CONTRACT_SYS_DEVICE, OP_RESET), OP_ENTRY_R(CONTRACT_PD, OP_LIMITS),
 * OP_DECLARE_READ(OWN_DC_MOTOR, OP_OUTPUT)): OP_DECLARE_READ / OP_DECLARE_WRITE write the side's cmd_f (the read
 * parses the key, the write the value), OP_ENTRY lists both sides, OP_ENTRY_R / OP_ENTRY_W one. Every row takes the
 * op's flags from its table (SYS_DEVICE_OP_ALWAYS, SYS_DEVICE_OP_LOCAL): a driver can't leave them out.
 * OP_DECLARE_WRITE_TAIL is the write of an op marked SYS_DEVICE_OP_TAIL (cmd_table.h, CMD_HANDLER_TAIL): its body
 * sees `args` (the head, the op's value_t, in place), `tail` and `tail_len`. The table says which of the two an op
 * takes, and the other one fails to compile:
 *   OP_DECLARE_WRITE_TAIL(CONTRACT_I2C, OP_RAW) { return raw_write(c, args->addr, args->reg, tail, tail_len); } */
#define SYS_DEVICE__ROW_FLAGS(P, name) .flags = P##_FLAGS_##name
#define OP_DECLARE_READ(P, name)    SYS_DEVICE__HANDLER(P##_r_##name, P##_key_##name##_t)
#define OP_DECLARE_WRITE(P, name)                                                                                  \
  _Static_assert((P##_FLAGS_##name & SYS_DEVICE_OP_TAIL) == 0, #P " " #name " has a tail: OP_DECLARE_WRITE_TAIL"); \
  SYS_DEVICE__HANDLER(P##_w_##name, P##_value_##name##_t)
#define OP_DECLARE_WRITE_TAIL(P, name)                                                                             \
  _Static_assert((P##_FLAGS_##name & SYS_DEVICE_OP_TAIL) != 0, #P " " #name " has no tail: OP_DECLARE_WRITE");     \
  CMD_HANDLER_TAIL(P##_w_##name, SYS_DEVICE_CTX, P##_value_##name##_t)
#define OP_ENTRY(P, name)                                                                     \
  {.op = P##_##name, .read = P##_r_##name, .write = P##_w_##name, SYS_DEVICE__ROW_FLAGS(P, name)}
#define OP_ENTRY_R(P, name) {.op = P##_##name, .read = P##_r_##name, SYS_DEVICE__ROW_FLAGS(P, name)}
#define OP_ENTRY_W(P, name) {.op = P##_##name, .write = P##_w_##name, SYS_DEVICE__ROW_FLAGS(P, name)}

/* A read handler answers with CMD_REPLY (cmd_table.h), its value from the table:
 *   return CMD_REPLY(CONTRACT_IO, OP_VOLTAGE, .pin = args->pin, .voltage_mV = mV);
 *   return CMD_REPLY(CONTRACT_IO, OP_LOCKED, c->pins.locked); */

/**
 * @brief A device's own ops (0xFFxx), written like a contract: one table P_OPERATIONS(X, P) of rows
 * X(P, OP_NAME, n, key_t, value_t, flags) in the driver's source, P the driver's prefix (OWN_<DEVICE>). Gives
 * P_<OP_NAME> = SYS_DEVICE_OWN_OP(n), P_FLAGS_<OP_NAME>, P_key_<OP_NAME>_t and P_value_<OP_NAME>_t, so an own op has
 * the handlers, the rows and the answer of a contract's (OP_DECLARE_READ, OP_DECLARE_WRITE, OP_DECLARE_WRITE_TAIL,
 * OP_ENTRY*, CMD_REPLY) and its flags come from the table: an op the timers' task calls is LOCAL there, not in a row
 * someone may forget.
 * @usage #define OWN_DC_MOTOR_OPERATIONS(X, P)                             \
 *          X(P, OP_SPEED, 0x00, cmd_none_t, dc_motor_speed_t, 0)           \
 *          X(P, OP_TICK,  0x08, cmd_none_t, cmd_none_t, SYS_DEVICE_OP_LOCAL)
 *        SYS_DEVICE_OWN_DECLARE(OWN_DC_MOTOR)  // OWN_DC_MOTOR_OP_SPEED = 0xFF00, OWN_DC_MOTOR_value_OP_SPEED_t, ...
 */
#define SYS_DEVICE_OWN_DECLARE(P) SYS_DEVICE__TABLE(P, SYS_DEVICE_CONTRACT_OWN)

/**
 * @brief A device type's own events: one table P_EVENTS(X, P) of rows X(P, NAME, code, flags, { payload }) in the
 * driver's source, under the module of the type's own events, SYS_EVENT_MODULE_TYPE(P_TYPE) (sys_event.h, "Events
 * tables"), in the lane DEVICE: after SYS_DEVICE_TYPE_DECLARE(P, ...). Gives P_EV_<NAME>, P_EV_<NAME>_FLAGS and
 * evt_data_P_EV_<NAME>_t.
 * @usage #define DC_MOTOR_EVENTS(X, P) X(P, STALL, 0x01, SYS_EVENT_NOTIFY, { int32_t mA; })
 *        SYS_DEVICE_OWN_EVENTS(DC_MOTOR)  // DC_MOTOR_EV_STALL = EVT_TAG(SYS_EVENT_MODULE_TYPE(DC_MOTOR_TYPE), 0x01)
 */
#define SYS_DEVICE_OWN_EVENTS(P) SYS_EVENT__TABLE(P, SYS_EVENT_MODULE_TYPE(P##_TYPE), DEVICE)

/**
 * @brief ERR_DEV_RANGE unless lo <= v <= hi. The bounds are written once: the check and the error's min / max come from
 * the same expressions. The value and the bounds are reported as int32_t (ERR_DEV_RANGE is signed).
 * The parameter is `dev`, not `id`: a parameter named `id` would also be replaced after the `.` of `.id =`.
 * @usage SYS_DEV_CHECK_IN_RANGE(id, a->duty, 0, IO_PWM_DUTY_MAX);
 */
#define SYS_DEV_CHECK_IN_RANGE(dev, v, lo, hi)                                                                   \
  ERR_CHECK_IN_RANGE((v), (lo), (hi), ERR_DEV_RANGE, .id = (dev), .value = (int32_t)(v), .min = (int32_t)(lo), \
                     .max = (int32_t)(hi))

/**
 * @brief One entry of a device's op table: the op and its two sides (NULL = the device has no such side). Each gets
 * the driver's struct as ctx and the side's args only.
 */
typedef struct {
  uint16_t op;
  cmd_f read;
  cmd_f write;
  /* SYS_DEVICE_OP_*: a row gets them from its table (OP_ENTRY*), a contract's or the device's own. */
  uint8_t flags;
} sys_device_op_t;

/**
 * @brief A device type as the registry keeps it: everything it needs to make, run and remove the type's instances. A
 * driver does not fill it: SYS_DEVICE_TYPE_DEFINE does, from the driver's typed hooks.
 */
typedef struct {
  uint16_t cfg_size;  // create struct, exactly (ERR_CMD_BAD_LEN otherwise)
  uint16_t ctx_size;  // the driver's struct, allocated zeroed by the registry
  /** Builds the device: checks the cfg, fills the struct, sets up the hardware. What it takes (a pin, a bus address, a
   *  subscription, a timer, a peripheral) is a resource of the device (sys_device_res_add; the take functions of the
   *  contracts add theirs). On a failure the registry releases the resources added so far and frees the struct. `id`
   *  is the device's ID; `cfg` is the packed create struct (cfg_size bytes; may be unaligned). */
  err_h (*create)(uint8_t id, void* ctx, const void* cfg);
  /** SUSPEND: the safe state (outputs, pins). The registry sets the flag afterwards, even when this fails (a
   *  half-safe device refuses writes). A NOLOCK op in flight is not waited for: it may still be running (the driver's
   *  to guard), and the next one of a write is refused (ERR_DEV_SUSPENDED, ALWAYS aside). It is also the device's last
   *  state: REMOVE runs it on a device not suspended yet (no NOLOCK op in flight), then releases the resources. */
  err_h (*suspend)(void* ctx);
  /** RESUME: back to work. Called only while suspended; the registry clears the flag when it succeeds. */
  err_h (*resume)(void* ctx);
  const sys_device_op_t* ops;  // the type's own ops (RESET is one); never REMOVE / INFO / OPS / SUSPEND / RESUME
  uint16_t op_count;
  /** Optional: false for an op of the table this device can't run as wired (the DRV8962's POWER_MONITOR without its
   *  IPROPI pins): left out of OPS and answered ERR_DEV_UNKNOWN_OP. NULL: every op of the table. */
  bool (*has_op)(const void* ctx, uint16_t op);
} sys_device_type_t;

/** @brief The first type ID a driver may take: the IDs below are reserved. */
#define SYS_DEVICE_TYPE_FIRST 0x0040u

/**
 * @brief Adds a type. Called by SYS_DEVICE_TYPE_DEFINE before app_main; asserts on a misdefined type or a taken ID.
 * @param[in] id   The type's ID (CREATE, INFO), at least SYS_DEVICE_TYPE_FIRST
 * @param[in] type The driver's type, kept (not copied)
 */
void sys_device_type_register(uint16_t id, const sys_device_type_t* type);

/**
 * @brief Gives the driver with prefix NAME its type ID, once, at the top of its source: NAME_TYPE, for everything that
 * is numbered by the type (the module of its own events, SYS_EVENT_MODULE_TYPE(NAME_TYPE)) and for
 * SYS_DEVICE_TYPE_DEFINE. The ID is the driver's own: no list of types to add it to. It is written as four hex digits
 * (0x0041): the macro defines the global symbol sys_device__type_0x0041, so two drivers with one ID fail to link
 * (multiple definition). The ID is wire format (CREATE, INFO): never renumber it.
 * @usage SYS_DEVICE_TYPE_DECLARE(PCA9685, 0x0041)  // PCA9685_TYPE
 */
#define SYS_DEVICE_TYPE_DECLARE(NAME, id)                                                      \
  _Static_assert((id) >= SYS_DEVICE_TYPE_FIRST, "device type ID below SYS_DEVICE_TYPE_FIRST"); \
  enum { NAME##_TYPE = (id) };                                                                 \
  __attribute__((used)) const uint8_t sys_device__type_##id = 0;

/* The type of a driver's hooks: the registry's, with the driver's struct and create struct in place of void. A hook of
 * another signature fails to compile where SYS_DEVICE_TYPE_DEFINE binds it. */
#define SYS_DEVICE__TYPE(NAME, cfg_t, create_fn, suspend_fn, resume_fn, k_ops, has_op_ptr)                    \
  _Static_assert(_Alignof(cfg_t) == 1, #cfg_t ": a create struct is packed (it is read where the frame is)"); \
  static err_h sys_device__create(uint8_t id, void* ctx, const void* cfg) {                                   \
    err_h (*const typed)(uint8_t, SYS_DEVICE_CTX*, const cfg_t*) = (create_fn);                               \
    return typed(id, ctx, cfg);                                                                               \
  }                                                                                                           \
  static err_h sys_device__suspend(void* ctx) {                                                               \
    err_h (*const typed)(SYS_DEVICE_CTX*) = (suspend_fn);                                                     \
    return typed(ctx);                                                                                        \
  }                                                                                                           \
  static err_h sys_device__resume(void* ctx) {                                                                \
    err_h (*const typed)(SYS_DEVICE_CTX*) = (resume_fn);                                                      \
    return typed(ctx);                                                                                        \
  }                                                                                                           \
  static const sys_device_type_t sys_device__type = {                                                         \
      .cfg_size = sizeof(cfg_t),                                                                              \
      .ctx_size = sizeof(SYS_DEVICE_CTX),                                                                     \
      .create   = sys_device__create,                                                                         \
      .suspend  = sys_device__suspend,                                                                        \
      .resume   = sys_device__resume,                                                                         \
      .ops      = (k_ops),                                                                                    \
      .op_count = sizeof(k_ops) / sizeof((k_ops)[0]),                                                         \
      .has_op   = (has_op_ptr),                                                                               \
  };                                                                                                          \
  __attribute__((constructor)) static void sys_device__type_register(void) {                                  \
    sys_device_type_register(NAME##_TYPE, &sys_device__type);                                                 \
  }

/**
 * @brief Defines the type of the driver with prefix NAME and registers it under NAME_TYPE before app_main (a
 * constructor): the last line of the driver's source. The sizes come from the types (cfg_t, SYS_DEVICE_CTX), the op
 * count from the table. The hooks are the driver's, typed:
 *   err_h create(uint8_t id, SYS_DEVICE_CTX* c, const cfg_t* cfg)  builds the device (sys_device_type_t.create);
 *                                                                  `cfg` is the frame's bytes, read in place
 *   err_h suspend(SYS_DEVICE_CTX* c)                               the safe state, and the device's last state
 *   err_h resume(SYS_DEVICE_CTX* c)                                back to work
 * All three are required. The component must be linked as WHOLE_ARCHIVE: nothing references it, and the linker would
 * drop the registration.
 * @usage SYS_DEVICE_TYPE_DEFINE(PCA9685, pca9685_cfg_t, create, suspend, resume, k_ops)
 */
#define SYS_DEVICE_TYPE_DEFINE(NAME, cfg_t, create_fn, suspend_fn, resume_fn, k_ops) \
  SYS_DEVICE__TYPE(NAME, cfg_t, create_fn, suspend_fn, resume_fn, k_ops, NULL)

/**
 * @brief SYS_DEVICE_TYPE_DEFINE for a type that can't run every op of its table on every device:
 * `bool has_op(const SYS_DEVICE_CTX* c, uint16_t op)` answers false for an op this device can't run as wired (the
 * DRV8962's POWER_MONITOR without its IPROPI pins); it is left out of OPS and answered ERR_DEV_UNKNOWN_OP.
 * @usage SYS_DEVICE_TYPE_DEFINE_HAS_OP(DRV8962, drv8962_cfg_t, create, suspend, resume, k_ops, has_op)
 */
#define SYS_DEVICE_TYPE_DEFINE_HAS_OP(NAME, cfg_t, create_fn, suspend_fn, resume_fn, k_ops, has_op_fn) \
  static bool sys_device__has_op(const void* ctx, uint16_t op) {                                       \
    bool (*const typed)(const SYS_DEVICE_CTX*, uint16_t) = (has_op_fn);                                \
    return typed(ctx, op);                                                                             \
  }                                                                                                    \
  SYS_DEVICE__TYPE(NAME, cfg_t, create_fn, suspend_fn, resume_fn, k_ops, sys_device__has_op)

/**
 * @brief Who called the op this task runs: the device whose op made the call (a driver calling the device it uses; in
 * create(), the device being created), SYS_DEVICE_ID_NONE for a call that comes from no device (a command, a module, a
 * listener). For a handler that keeps an owner of what it gives out (a pin's lock, a bus address). Outside an op:
 * SYS_DEVICE_ID_NONE.
 * @return The calling device's ID, or SYS_DEVICE_ID_NONE
 */
uint8_t sys_device_caller(void);

/**
 * @brief The device whose op (or create(), suspend(), resume()) this task runs now. Outside an op: SYS_DEVICE_ID_NONE.
 * @return The running device's ID, or SYS_DEVICE_ID_NONE
 */
uint8_t sys_device_self(void);

/**
 * @brief Whether the device whose op this task runs now is suspended (for a READ or SYS_DEVICE_OP_ALWAYS op such as
 * ON_EVENT, to return a cached copy or skip hardware access; in suspend() it is still false, in resume() still true).
 * Outside an op: false.
 * @return true when running an op of an installed suspended device
 */
bool sys_device_self_suspended(void);

/**
 * @brief The generation of device `id`: it tells the device under an ID from the ones before it (the `gen` of
 * SYS_DEVICE_EV_CREATED). In create() it is already the new device's, so what create() hands out by ID and generation
 * (a timer) names this device only. Any task, no lock: exact in the device's own ops, a snapshot elsewhere.
 * @param[in] id Device ID
 * @return 1..255; 0 before the ID's first device and past SYS_DEVICE_COUNT
 */
uint8_t sys_device_gen(uint8_t id);

/**
 * @brief Target op on a specific device instance: `[op u16][gen u8][id u8]` packed into `void*` for timer and event
 * callbacks, so neither holds a pointer to a device that can go away and a stale expiry/delivery never hits a later
 * device under the same ID.
 */
typedef struct {
  uint8_t id;
  uint8_t gen;
  uint16_t op;
} sys_device_target_t;

_Static_assert(sizeof(uintptr_t) >= sizeof(uint32_t), "sys_device_target_t packs into void*");

static inline void* sys_device_target_pack(sys_device_target_t target) {
  return (void*)(uintptr_t)(((uint32_t)target.op << 16) | ((uint32_t)target.gen << 8) | target.id);
}

static inline sys_device_target_t sys_device_target_unpack(const void* ctx) {
  const uintptr_t packed = (uintptr_t)ctx;
  return (sys_device_target_t){
      .id = (uint8_t)(packed & 0xFFu),
      .gen = (uint8_t)((packed >> 8) & 0xFFu),
      .op = (uint16_t)(packed >> 16),
  };
}

/**
 * @brief A step of a device that runs by itself (a timer's expiry, sys_device_timer.h): the write of its own op `op`,
 * without args, to device `id` of generation `gen`, made only when the device takes it now. A step is skipped, with
 * no error, when the device is gone or is another one (its generation), is suspended, is refused by the safe-state
 * sweep, or its lock is not free at once: the next step comes in its own time, and a step never waits for a device
 * another task holds. So the op needs no SYS_DEVICE_OP_ALWAYS and its driver no flag of its own for the safe state.
 * Any task but an ISR.
 * @param[in] id  Device ID
 * @param[in] gen The generation the step is for: sys_device_gen() in the device's create()
 * @param[in] op  An own op of the device (SYS_DEVICE_OWN_OP), LOCAL in its table
 * @return NULL when the op ran or the step was skipped; ERR_DEV_BAD_ARG for generation 0 or an op not the device's
 *         own; else the op's error, wrapped like a call's (ERR_DEV_OP, ERR_DEV_ID)
 */
MUST_USE err_h sys_device_step(uint8_t id, uint8_t gen, uint16_t op);

/**
 * @brief Delivers a payload (an event) to device `id` of generation `gen` by writing `op`. Unlike sys_device_step(),
 * this is not skipped when busy or suspended: it waits for the slot lock (with the call timeout) and runs on a
 * suspended device if the op is marked SYS_DEVICE_OP_ALWAYS (so the handler can clear hardware lines or skip via
 * sys_device_self_suspended()). When the device is gone or another device holds the ID (generation mismatch), the
 * delivery is skipped with NULL.
 * @param[in] id       Device ID
 * @param[in] gen      The generation the delivery is for: sys_device_gen() at subscription time
 * @param[in] op       Target write op (an own op or contract event op; never a registry op)
 * @param[in] args     Payload bytes (NULL if args_len is 0)
 * @param[in] args_len Length in bytes
 * @return NULL on delivery or stale-generation skip; ERR_DEV_BAD_ID, ERR_DEV_BAD_ARG, or the call's error wrapped
 */
MUST_USE err_h sys_device_deliver(uint8_t id, uint8_t gen, uint16_t op, const void* args, uint32_t args_len);

/**
 * @brief Gives back one resource of a device: `arg` is what it was added with. It releases only what was taken (a
 * resource is added before it is taken, so the function is guarded by the resource's own state).
 * @return The error of the release; the other resources are still released
 */
typedef err_h (*sys_device_res_f)(void* arg);

/** @brief A resource of a device, in the driver's struct (zeroed: not added). Its fields are the registry's. */
typedef struct sys_device_res_s {
  struct sys_device_res_s* next;
  sys_device_res_f release;  /**< NULL: not added */
  void* arg;
} sys_device_res_t;

/**
 * @brief Makes `res` a resource of device `owner`: the registry calls `release(arg)` when the device is removed and
 * when its create() fails, the last added first, with the lock of `owner` held and the driver's struct still there.
 * Called from the owner's create() (or an op of it), before the resource is taken; a driver has no teardown of its own.
 * @param[in]  owner   The device being created
 * @param[out] res     The resource's node, in the driver's struct; not added yet
 * @param[in]  release Gives the resource back
 * @param[in]  arg     Passed to `release`; valid as long as the driver's struct
 * @return ERR_DEV_BAD_ID{owner}, ERR_DEV_BAD_ARG for a NULL or a node added already, ERR_DEV_NOT_LOCKED{owner} for
 *         a caller without the lock of `owner`
 * @usage ERR_TRY(sys_device_res_add(id, &c->bus_res, bus_release, c));
 *        ERR_TRY(ERR_ESP(i2c_new_master_bus(&cfg, &c->bus)));  // bus_release deletes c->bus unless it is NULL
 */
MUST_USE err_h sys_device_res_add(uint8_t owner, sys_device_res_t* res, sys_device_res_f release, void* arg);

/**
 * @brief Declares that device `user` uses device `dep` (one of its pins, a bus device...). Called by the driver's
 * create() for every device it will call, before using it. The registry then refuses to REMOVE `dep` while `user`
 * exists (ERR_DEV_IN_USE) and sweeps in an order where users come first. Declaring the same dep twice is fine.
 * @param[in] user The device being created
 * @param[in] dep  The device used: it exists, and is not `user`
 * @return ERR_DEV_NOT_FOUND{dep} unless it exists and is not being removed, ERR_DEV_CYCLE{user, dep} when `dep`
 *         uses `user` already, itself or through other devices
 * @usage if (sys_io_pin_valid(c->oe)) { ERR_TRY(sys_device_use(id, c->oe.device)); }
 */
MUST_USE err_h sys_device_use(uint8_t user, uint8_t dep);

/**
 * @brief Like sys_device_use(), and checks that `dep` runs `op` on `dir` as wired, so a wrong pin reference fails in
 * create() and not at the first write.
 * @param[in] user The device being created
 * @param[in] dep  The device used
 * @param[in] op   Op number, SYS_DEVICE_OP(contract, n)
 * @param[in] dir  cmd_dir_e: the side the driver will call
 * @return ERR_DEV_NOT_FOUND{dep} unless it exists, ERR_DEV_UNKNOWN_OP{op} unless it has that op and side
 * @usage ERR_TRY(sys_device_require_op(id, c->oe.device, CONTRACT_IO_OP_LEVEL, CMD_DIR_WRITE));
 */
MUST_USE err_h sys_device_require_op(uint8_t user, uint8_t dep, uint16_t op, uint8_t dir);

#ifdef __cplusplus
}
#endif
