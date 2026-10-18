#include "sys_device.h"

#include "freertos/FreeRTOS.h"
#include "sdkconfig.h"
#include "utils.h"

ERR_MODULE_DEFINE(ERR_MOD_SYS_DEVICE, "sys_device", ERR_SYS_DEVICE_MAP)

// A type ID listed twice in sys_device_types.h is a duplicate case label: a compile error.
#define SYS_DEVICE__TYPE_CASE(name, id) \
  case (id):                            \
    break;
__attribute__((unused)) static void type_ids_unique(uint16_t t) {
  switch (t) {
    SYS_DEVICE_TYPE_LIST(SYS_DEVICE__TYPE_CASE)
    default:
      break;
  }
}

typedef struct {
  uint16_t id;
  const sys_device_type_t* type;
  sys_device_base_t* ctx;
  sys_device_error_policy_t policy;  // SET_ERROR_POLICY; read by the error path under s_table_mux
  bool system;                       // the board's own (sys_device_mark_system): sys_device_remove_all leaves it
} dev_t_;

static const sys_device_type_t* s_types[CONFIG_SYS_DEVICE_TYPES];  // filled by constructors, before any task
static uint32_t s_type_count;

/* In creation order. A device can only use devices that exist when it is created, and a used device can't be
 * removed, so this order is a dependency order: the sweeps go newest first (SUSPEND, REMOVE) or oldest first
 * (RESUME), whatever the IDs. */
static dev_t_ s_devs[CONFIG_SYS_DEVICE_MAX];
static uint32_t s_dev_count;

static sys_device_state_f s_state_listener;
// A sweep in progress (per registry lock, so one task): its devices' changes make one call at the end, not one each.
static uint32_t s_sweep_depth;
static bool s_sweep_changed;
static bool s_creating;  // a create() is running: another CREATE from inside it would break the table's invariants

/* One recursive lock for the registry and for every call into a device (sys_device.h "Locking"). */
U_RECURSIVE_MUTEX_DEFINE(sys_device_mutex);

#define LOCK() U_RECURSIVE_MUTEX_LOCK(sys_device_mutex, WAIT_FOREVER)
#define UNLOCK() U_RECURSIVE_MUTEX_UNLOCK(sys_device_mutex)

/* The error path reads a device's policy without the registry lock (an op holding it may wait for the reporting
 * task). Changes of the table (add, remove) and of a policy take this one too, briefly. */
static portMUX_TYPE s_table_mux = portMUX_INITIALIZER_UNLOCKED;

static const sys_device_error_policy_t k_policy_default = {.importance = SYS_DEVICE_IMPORTANCE_LOW};

static uint16_t u16_at(const uint8_t* b) {
  return (uint16_t)(b[0] | (b[1] << 8));
}

/* ---- Types ---- */

static const sys_device_type_t* find_type(uint16_t type) {
  for (uint32_t i = 0; i < s_type_count; i++) {
    if (s_types[i]->type == type) {
      return s_types[i];
    }
  }
  return NULL;
}

static bool has_op(const sys_device_type_t* t, uint16_t op) {
  for (uint32_t i = 0; i < t->op_count; i++) {
    if (t->ops[i].op == op) {
      return true;
    }
  }
  return false;
}

// Constructors run one at a time before the scheduler: no lock. A misdefined type stops the boot, like a
// duplicate error module (err_register_module).
void sys_device_type_register(const sys_device_type_t* type) {
  assert(type != NULL && type->create != NULL && type->uninstall != NULL);
  assert(type->ctx_size >= sizeof(sys_device_base_t) && "the device's struct starts with sys_device_base_t");
  assert(type->suspend != NULL && type->resume != NULL && "a device type must have a SUSPEND and a RESUME hook");
  assert(!has_op(type, SYS_DEVICE_OP_SUSPEND) && !has_op(type, SYS_DEVICE_OP_RESUME) &&
         "SUSPEND / RESUME are the type's hooks, not table rows");
  assert(!has_op(type, SYS_DEVICE_OP_REMOVE) && !has_op(type, SYS_DEVICE_OP_INFO) && !has_op(type, SYS_DEVICE_OP_OPS) &&
         !has_op(type, SYS_DEVICE_OP_SET_ERROR_POLICY) && !has_op(type, SYS_DEVICE_OP_GET_ERROR_POLICY) &&
         "REMOVE / INFO / OPS / SET_ERROR_POLICY / GET_ERROR_POLICY are answered by the registry");
  assert(find_type(type->type) == NULL && "device type registered twice");
  if (s_type_count >= CONFIG_SYS_DEVICE_TYPES) {
    assert(!"more device types than SYS_DEVICE_TYPES");
    return;
  }
  s_types[s_type_count++] = type;
}

/* ---- Instances and dependencies (lock held) ---- */

// Index of `id`, or s_dev_count if it isn't registered.
static uint32_t find_dev(uint16_t id) {
  for (uint32_t i = 0; i < s_dev_count; i++) {
    if (s_devs[i].id == id) {
      return i;
    }
  }
  return s_dev_count;
}

static bool uses(const sys_device_base_t* d, uint16_t dep) {
  for (uint32_t i = 0; i < d->dep_count; i++) {
    if (d->deps[i] == dep) {
      return true;
    }
  }
  return false;
}

// Index of a device that uses `id`, or s_dev_count if none does.
static uint32_t find_user(uint16_t id) {
  for (uint32_t i = 0; i < s_dev_count; i++) {
    if (uses(s_devs[i].ctx, id)) {
      return i;
    }
  }
  return s_dev_count;
}

err_h sys_device_use(sys_device_base_t* dev, uint16_t dep) {
  err_h err = NULL;
  LOCK();  // recursive: create() runs under it
  if (dep == dev->id || find_dev(dep) == s_dev_count) {
    err = ERR_NEW(ERR_DEV_NOT_FOUND, .id = dep);
  } else if (!uses(dev, dep)) {
    if (dev->dep_count == CONFIG_SYS_DEVICE_MAX_DEPS) {
      err = ERR_NEW(ERR_DEV_DEPS_FULL, .id = dev->id);
    } else {
      dev->deps[dev->dep_count++] = dep;
    }
  }
  UNLOCK();
  return err;
}

/* ---- Generic ops ---- */

// CREATE of a free ID, table not full. args = [type u16][create struct].
static err_h create(uint16_t id, const uint8_t* args, uint32_t args_len) {
  ERR_CHECK(!s_creating, ERR_DEV_BAD_ARG);  // a create() creating another device: not supported
  ERR_CHECK(args_len >= 2, ERR_CMD_BAD_LEN, .got = (uint16_t)args_len, .need = 2);  // the type u16
  const uint16_t type_id = u16_at(args);
  const sys_device_type_t* type = find_type(type_id);
  ERR_CHECK(type != NULL, ERR_DEV_TYPE_UNKNOWN, .type = type_id);
  ERR_CHECK(args_len - 2 == type->cfg_size, ERR_CMD_BAD_LEN, .got = (uint16_t)(args_len - 2),
            .need = type->cfg_size);
  sys_device_base_t* ctx = calloc(1, type->ctx_size);
  ERR_CHECK(ctx != NULL, ERR_DEV_NO_MEM, .id = id, .size = type->ctx_size);
  ctx->id = id;
  s_creating = true;
  err_h err = type->create(ctx, args + 2);
  s_creating = false;
  if (err != NULL) {
    SYS_DEVICE_KEEP_FIRST(err, type->uninstall(ctx));  // the cause stays; a rollback failure is dropped
    free(ctx);
    return ERR_WRAP(err, ERR_DEV_INSTALL, .id = id, .type = type_id);
  }
  taskENTER_CRITICAL(&s_table_mux);
  s_devs[s_dev_count++] = (dev_t_){.id = id, .type = type, .ctx = ctx, .policy = k_policy_default};  // newest last
  taskEXIT_CRITICAL(&s_table_mux);
  return NULL;
}

// Refused while another device uses it. Otherwise the hardware is released, the instance unregistered and
// freed, even if uninstall() failed.
static err_h remove_dev(uint16_t id) {
  const uint32_t user = find_user(id);
  ERR_CHECK(user == s_dev_count, ERR_DEV_IN_USE, .id = id, .user = s_devs[user].id);
  const dev_t_ d = s_devs[find_dev(id)];
  err_h err = d.type->uninstall(d.ctx);
  taskENTER_CRITICAL(&s_table_mux);
  const uint32_t j = find_dev(id);  // re-found: uninstall called other devices
  memmove(&s_devs[j], &s_devs[j + 1], (s_dev_count - j - 1) * sizeof(s_devs[0]));  // keeps the creation order
  s_dev_count--;
  taskEXIT_CRITICAL(&s_table_mux);
  free(d.ctx);
  return err;
}

static err_h reply_info(const dev_t_* d, uint8_t* out, uint32_t out_cap, uint32_t* out_len) {
  const sys_device_info_t info = {.type_id = d->type->type, .suspended = d->ctx->suspended};
  return cmd_reply(out, out_cap, out_len, &info, sizeof(info));
}

// OPS: the generic ops, then the type's table.
static err_h reply_ops(const sys_device_type_t* t, uint8_t* out, uint32_t out_cap, uint32_t* out_len) {
  static const uint16_t k_generic[] = {SYS_DEVICE_OP_SUSPEND, SYS_DEVICE_OP_RESUME, SYS_DEVICE_OP_REMOVE,
                                       SYS_DEVICE_OP_INFO, SYS_DEVICE_OP_OPS, SYS_DEVICE_OP_SET_ERROR_POLICY,
                                       SYS_DEVICE_OP_GET_ERROR_POLICY};
  const uint32_t count = ARRAY_LEN(k_generic) + t->op_count;
  ERR_CHECK(count * 2 <= out_cap, ERR_CMD_OUT_FULL, .need = (uint16_t)(count * 2), .cap = (uint16_t)out_cap);
  for (uint32_t i = 0; i < count; i++) {
    const uint16_t op = i < ARRAY_LEN(k_generic) ? k_generic[i] : t->ops[i - ARRAY_LEN(k_generic)].op;
    out[i * 2] = (uint8_t)op;
    out[i * 2 + 1] = (uint8_t)(op >> 8);
  }
  *out_len = count * 2;
  return NULL;
}

// SET_ERROR_POLICY: any action ID (0 = none; it may be recorded later), importance NONE..CRITICAL. Allowed while
// suspended: it configures the registry, not the device.
static err_h set_policy(uint32_t index, const uint8_t* args, uint32_t args_len) {
  sys_device_error_policy_t p;
  ERR_CHECK(args_len == sizeof(p), ERR_CMD_BAD_LEN, .got = (uint16_t)args_len, .need = sizeof(p));
  memcpy(&p, args, sizeof(p));
  SYS_DEV_CHECK_IN_RANGE(s_devs[index].id, p.importance, SYS_DEVICE_IMPORTANCE_NONE, SYS_DEVICE_IMPORTANCE_CRITICAL);
  taskENTER_CRITICAL(&s_table_mux);
  s_devs[index].policy = p;
  taskEXIT_CRITICAL(&s_table_mux);
  return NULL;
}

bool sys_device_error_policy_get(uint16_t id, sys_device_error_policy_t* out) {
  taskENTER_CRITICAL(&s_table_mux);
  const uint32_t i = find_dev(id);
  const bool found = i != s_dev_count;
  if (found) {
    *out = s_devs[i].policy;
  }
  taskEXIT_CRITICAL(&s_table_mux);
  return found;
}

void sys_device_set_state_listener(sys_device_state_f fn) {
  s_state_listener = fn;
}

// The flag of device `id` changed: told now, or once at the end of the sweep in progress.
static void state_changed(uint16_t id, bool suspended) {
  if (s_sweep_depth > 0) {
    s_sweep_changed = true;
  } else if (s_state_listener != NULL) {
    s_state_listener(id, suspended);
  }
}

// SUSPEND and RESUME: the registry owns the flag, the driver's hook does the hardware (sys_device.h, the type).
static err_h run_lifecycle(const dev_t_* d, uint16_t op, const uint8_t* args, uint32_t args_len) {
  ERR_CHECK(args_len == 0, ERR_CMD_BAD_LEN, .got = (uint16_t)args_len, .need = 0);
  sys_device_base_t* base = d->ctx;
  if (op == SYS_DEVICE_OP_SUSPEND) {
    const bool was = base->suspended;
    const err_h err = d->type->suspend(d->ctx);
    base->suspended = true;  // even when the hook failed: a half-safe device refuses writes
    if (!was) {
      state_changed(d->id, true);
    }
    return err;
  }
  if (!base->suspended) {
    return NULL;
  }
  const err_h err = d->type->resume(d->ctx);
  if (err == NULL) {
    base->suspended = false;  // a failing resume leaves the device suspended
    state_changed(d->id, false);
  }
  return err;
}

// Any op but CREATE, on a registered device.
static err_h run_op(uint32_t index, uint16_t op, const uint8_t* args, uint32_t args_len, uint8_t* out,
                    uint32_t out_cap, uint32_t* out_len) {
  const dev_t_ d = s_devs[index];
  if (op == SYS_DEVICE_OP_SUSPEND || op == SYS_DEVICE_OP_RESUME) {
    return run_lifecycle(&d, op, args, args_len);
  }
  if (op == SYS_DEVICE_OP_SET_ERROR_POLICY) {
    return set_policy(index, args, args_len);
  }
  if (op == SYS_DEVICE_OP_GET_ERROR_POLICY) {
    ERR_CHECK(args_len == 0, ERR_CMD_BAD_LEN, .got = (uint16_t)args_len, .need = 0);
    return cmd_reply(out, out_cap, out_len, &d.policy, sizeof(d.policy));
  }
  if (op == SYS_DEVICE_OP_REMOVE || op == SYS_DEVICE_OP_INFO || op == SYS_DEVICE_OP_OPS) {
    ERR_CHECK(args_len == 0, ERR_CMD_BAD_LEN, .got = (uint16_t)args_len, .need = 0);
    if (op == SYS_DEVICE_OP_REMOVE) {
      return remove_dev(d.id);
    }
    return op == SYS_DEVICE_OP_INFO ? reply_info(&d, out, out_cap, out_len) : reply_ops(d.type, out, out_cap, out_len);
  }
  for (uint32_t i = 0; i < d.type->op_count; i++) {
    if (d.type->ops[i].op == op) {
      return d.type->ops[i].fn(d.ctx, args, args_len, out, out_cap, out_len);
    }
  }
  return ERR_NEW(ERR_DEV_UNKNOWN_OP, .op = op);
}

static err_h wrap_op(err_h err, uint16_t op) {
  return err != NULL ? ERR_WRAP(err, ERR_DEV_OP, .op = op) : NULL;
}

static err_h sweep(uint16_t op, bool newest_first, bool project_only);

/* One call, the args in place (no copy, so no size limit besides the caller's). Errors are wrapped in ERR_DEV_ID. */
static err_h call(uint16_t id, uint16_t op, const uint8_t* args, uint32_t args_len, uint8_t* out, uint32_t out_cap,
                  uint32_t* out_len) {
  // SUSPEND / RESUME / RESET to "no device" = every device (the sweeps). Not wrapped in ERR_DEV_ID{NONE}: the sweep's
  // own error already names the device that failed, and the error policy reads that ID.
  if (id == SYS_DEVICE_ID_NONE && (op == SYS_DEVICE_OP_SUSPEND || op == SYS_DEVICE_OP_RESUME || op == SYS_DEVICE_OP_RESET)) {
    ERR_CHECK(args_len == 0, ERR_CMD_BAD_LEN, .got = (uint16_t)args_len, .need = 0);
    return sweep(op, op != SYS_DEVICE_OP_RESUME, false);  // newest first, except RESUME
  }
  err_h err = NULL;
  LOCK();
  const uint32_t i = find_dev(id);
  const bool found = i != s_dev_count;  // before the op: REMOVE shrinks the table
  if (op == SYS_DEVICE_OP_CREATE) {
    if (id == SYS_DEVICE_ID_NONE) {
      err = ERR_NEW(ERR_DEV_BAD_ID, .id = id);
    } else if (found) {
      err = ERR_NEW(ERR_DEV_ID_TAKEN, .id = id);
    } else if (s_dev_count == CONFIG_SYS_DEVICE_MAX) {
      err = ERR_NEW(ERR_DEV_TABLE_FULL, .id = id);
    } else {
      err = create(id, args, args_len);
    }
  } else if (!found) {
    err = ERR_NEW(ERR_DEV_NOT_FOUND, .id = id);
  } else {
    err = run_op(i, op, args, args_len, out, out_cap, out_len);
  }
  UNLOCK();
  if (op == SYS_DEVICE_OP_CREATE || found) {
    err = wrap_op(err, op);  // not around NOT_FOUND: there was no op to run
  }
  return err != NULL ? ERR_WRAP(err, ERR_DEV_ID, .id = id) : NULL;
}

/* ---- Guards ---- */

typedef struct {
  uint8_t contract;
  sys_device_guard_f fn;
} guard_t;

static guard_t s_guards[CONFIG_SYS_DEVICE_GUARDS];  // set at boot, read without a lock afterwards
static uint32_t s_guard_count;

err_h sys_device_set_guard(uint8_t contract, sys_device_guard_f fn) {
  ERR_CHECK(fn != NULL && contract != SYS_DEVICE_CONTRACT_DEVICE, ERR_DEV_BAD_ARG);
  for (uint32_t i = 0; i < s_guard_count; i++) {
    ERR_CHECK(s_guards[i].contract != contract, ERR_DEV_BAD_ARG);  // one guard per contract
  }
  ERR_CHECK(s_guard_count < CONFIG_SYS_DEVICE_GUARDS, ERR_DEV_BAD_ARG);
  s_guards[s_guard_count++] = (guard_t){.contract = contract, .fn = fn};
  return NULL;
}

static sys_device_guard_f find_guard(uint16_t op) {
  for (uint32_t i = 0; i < s_guard_count; i++) {
    if (s_guards[i].contract == (uint8_t)(op >> 8)) {
      return s_guards[i].fn;
    }
  }
  return NULL;
}

static err_h call_checked(bool guarded, uint16_t id, uint16_t op, const void* args, uint32_t args_len, void* out,
                          uint32_t out_cap, uint32_t* out_len) {
  static const uint8_t k_no_args[1];  // handlers memcpy their args: never from NULL
  uint32_t dummy;
  ERR_CHECK(args != NULL || args_len == 0, ERR_DEV_BAD_ARG);
  if (out_len == NULL) {
    out_len = &dummy;
  }
  *out_len = 0;
  args = args != NULL ? args : k_no_args;
  /* The guard runs before the registry lock: it takes its own lock and then calls the device (sys_device_call_direct),
   * the same order as its own task. Its own failures get the op and device context like the device's would. */
  sys_device_guard_f guard = guarded ? find_guard(op) : NULL;
  if (guard != NULL) {
    bool handled = false;
    err_h err = guard(id, op, args, args_len, &handled);
    if (handled) {
      if (err != NULL && err->tag != ERR_DEV_ID) {
        err = ERR_WRAP(wrap_op(err, op), ERR_DEV_ID, .id = id);
      }
      return err;
    }
    err_free(err);  // a guard that passes the op on returns nothing
  }
  return call(id, op, args, args_len, out, out_cap, out_len);
}

err_h sys_device_call(uint16_t id, uint16_t op, const void* args, uint32_t args_len, void* out, uint32_t out_cap,
                      uint32_t* out_len) {
  return call_checked(true, id, op, args, args_len, out, out_cap, out_len);
}

err_h sys_device_call_direct(uint16_t id, uint16_t op, const void* args, uint32_t args_len, void* out,
                             uint32_t out_cap, uint32_t* out_len) {
  return call_checked(false, id, op, args, args_len, out, out_cap, out_len);
}

/* ----------------------------------------------------------------------------
 * Sweeps: best effort, the first error is returned, the rest reported
 * ------------------------------------------------------------------------- */

static err_h sweep(uint16_t op, bool newest_first, bool project_only) {
  err_h first = NULL;
  LOCK();
  // A snapshot of the IDs: REMOVE changes the list while we walk it.
  uint16_t ids[CONFIG_SYS_DEVICE_MAX];
  uint32_t n = 0;
  for (uint32_t i = 0; i < s_dev_count; i++) {
    const dev_t_* d = &s_devs[newest_first ? s_dev_count - 1 - i : i];
    if (!(project_only && d->system)) {
      ids[n++] = d->id;
    }
  }
  const bool outer = s_sweep_depth++ == 0;
  if (outer) {
    s_sweep_changed = false;
  }
  for (uint32_t i = 0; i < n; i++) {
    err_h err = sys_device_call(ids[i], op, NULL, 0, NULL, 0, NULL);
    if (err == NULL) {
      continue;
    }
    if (first == NULL) {
      first = err;
    } else {
      err_report(err);
    }
  }
  s_sweep_depth--;
  if (outer && s_sweep_changed && s_state_listener != NULL) {
    s_state_listener(SYS_DEVICE_ID_NONE, op == SYS_DEVICE_OP_SUSPEND);
  }
  UNLOCK();
  return first;
}

// Users before the devices they use.
err_h sys_device_suspend_all(void) {
  return sweep(SYS_DEVICE_OP_SUSPEND, true, false);
}

// The used devices before their users.
err_h sys_device_resume_all(void) {
  return sweep(SYS_DEVICE_OP_RESUME, false, false);
}

// Users before the devices they use, as suspend: a driver resets its outputs before the pins it drives go idle.
err_h sys_device_reset_all(void) {
  return sweep(SYS_DEVICE_OP_RESET, true, false);
}

err_h sys_device_remove_all(void) {
  return sweep(SYS_DEVICE_OP_REMOVE, true, true);
}

void sys_device_mark_system(void) {
  LOCK();
  taskENTER_CRITICAL(&s_table_mux);
  for (uint32_t i = 0; i < s_dev_count; i++) {
    s_devs[i].system = true;
  }
  taskEXIT_CRITICAL(&s_table_mux);
  UNLOCK();
}
