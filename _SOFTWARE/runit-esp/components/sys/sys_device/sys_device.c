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
  bool system;                       // the board's own (sys_device_mark_system): REMOVE sweep leaves it
} dev_t_;

typedef struct {
  uint8_t contract;
  sys_device_guard_f fn;
} guard_t;

static struct {
  const sys_device_type_t* types[CONFIG_SYS_DEVICE_TYPES];  // filled by constructors, before any task
  uint32_t type_count;
  /* In creation order. A device can only use devices that exist when it is created, and a used device can't be
   * removed, so this order is a dependency order: the sweeps go newest first (SUSPEND, REMOVE) or oldest first
   * (RESUME), whatever the IDs. */
  dev_t_ devs[CONFIG_SYS_DEVICE_MAX];
  uint32_t dev_count;
  guard_t guards[CONFIG_SYS_DEVICE_GUARDS];  // set at boot, read without a lock afterwards
  uint32_t guard_count;
  sys_device_state_f state_listener;
  // A sweep in progress (per registry lock, so one task): its devices' changes make one call at the end, not one each.
  uint32_t sweep_depth;
  bool sweep_changed;
  bool creating;  // a create() is running: another CREATE from inside it would break the table's invariants
} s_reg;  // zero at the start (.bss)

/* The error path reads a device's policy without the registry lock (an op holding it may wait for the reporting
 * task). Changes of the table (add, remove) and of a policy take this one too, briefly. */
static portMUX_TYPE s_table_mux = portMUX_INITIALIZER_UNLOCKED;

/* One recursive lock for the registry and for every call into a device (sys_device.h "Locking"). */
U_RECURSIVE_MUTEX_DEFINE(sys_device_mutex);

#define LOCK() U_RECURSIVE_MUTEX_LOCK(sys_device_mutex, WAIT_FOREVER)
#define UNLOCK() U_RECURSIVE_MUTEX_UNLOCK(sys_device_mutex)

static const sys_device_error_policy_t k_policy_default = {.importance = SYS_DEVICE_IMPORTANCE_LOW};

/* ---- Lookups (lock held) ---- */

static const sys_device_type_t* find_type(uint16_t type) {
  for (uint32_t i = 0; i < s_reg.type_count; i++) {
    if (s_reg.types[i]->type == type) {
      return s_reg.types[i];
    }
  }
  return NULL;
}

// The handler of `op` in the type's table, or NULL.
static cmd_f type_op(const sys_device_type_t* t, uint16_t op) {
  for (uint32_t i = 0; i < t->op_count; i++) {
    if (t->ops[i].op == op) {
      return t->ops[i].fn;
    }
  }
  return NULL;
}

// Index of `id`, or s_reg.dev_count if it isn't registered.
static uint32_t find_dev(uint16_t id) {
  for (uint32_t i = 0; i < s_reg.dev_count; i++) {
    if (s_reg.devs[i].id == id) {
      return i;
    }
  }
  return s_reg.dev_count;
}

static bool uses(const sys_device_base_t* d, uint16_t dep) {
  for (uint32_t i = 0; i < d->dep_count; i++) {
    if (d->deps[i] == dep) {
      return true;
    }
  }
  return false;
}

// Index of a device that uses `id`, or s_reg.dev_count if none does.
static uint32_t find_user(uint16_t id) {
  for (uint32_t i = 0; i < s_reg.dev_count; i++) {
    if (uses(s_reg.devs[i].ctx, id)) {
      return i;
    }
  }
  return s_reg.dev_count;
}

err_h sys_device_use(sys_device_base_t* dev, uint16_t dep) {
  err_h err = NULL;
  LOCK();  // recursive: create() runs under it
  if (dep == dev->id || find_dev(dep) == s_reg.dev_count) {
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

/* ---- CREATE ---- */

// CREATE of a free ID, table not full. args = [type u16][create struct].
static err_h create(uint16_t id, const uint8_t* args, uint32_t args_len) {
  ERR_CHECK(!s_reg.creating, ERR_DEV_BAD_ARG);  // a create() creating another device: not supported
  ERR_CHECK(args_len >= 2, ERR_CMD_BAD_LEN, .got = (uint16_t)args_len, .need = 2);  // the type u16
  uint16_t type_id;
  memcpy(&type_id, args, sizeof(type_id));  // little-endian target
  const sys_device_type_t* type = find_type(type_id);
  ERR_CHECK(type != NULL, ERR_DEV_TYPE_UNKNOWN, .type = type_id);
  ERR_CHECK(args_len - 2 == type->cfg_size, ERR_CMD_BAD_LEN, .got = (uint16_t)(args_len - 2),
            .need = type->cfg_size);
  sys_device_base_t* ctx = calloc(1, type->ctx_size);
  ERR_CHECK(ctx != NULL, ERR_DEV_NO_MEM, .id = id, .size = type->ctx_size);
  ctx->id = id;
  s_reg.creating = true;
  err_h err = type->create(ctx, args + 2);
  s_reg.creating = false;
  if (err != NULL) {
    ERR_KEEP_FIRST(err, type->uninstall(ctx));  // the cause stays; a rollback failure is dropped
    free(ctx);
    return ERR_WRAP(err, ERR_DEV_INSTALL, .id = id, .type = type_id);
  }
  taskENTER_CRITICAL(&s_table_mux);
  // newest last
  s_reg.devs[s_reg.dev_count++] = (dev_t_){.id = id, .type = type, .ctx = ctx, .policy = k_policy_default};
  taskEXIT_CRITICAL(&s_table_mux);
  return NULL;
}

/* ---- Error policy and state listener ---- */

bool sys_device_error_policy_get(uint16_t id, sys_device_error_policy_t* out) {
  taskENTER_CRITICAL(&s_table_mux);
  const uint32_t i = find_dev(id);
  const bool found = i != s_reg.dev_count;
  if (found) {
    *out = s_reg.devs[i].policy;
  }
  taskEXIT_CRITICAL(&s_table_mux);
  return found;
}

void sys_device_set_state_listener(sys_device_state_f fn) {
  s_reg.state_listener = fn;
}

// The flag of device `id` changed: told now, or once at the end of the sweep in progress.
static void state_changed(uint16_t id, bool suspended) {
  if (s_reg.sweep_depth > 0) {
    s_reg.sweep_changed = true;
  } else if (s_reg.state_listener != NULL) {
    s_reg.state_listener(id, suspended);
  }
}

/* ----------------------------------------------------------------------------
 * The registry's ops: the same on every device, never in a type's table. `i` is the device's index (lock held); the
 * args are checked to be exactly `args_size` bytes before.
 * ------------------------------------------------------------------------- */

// SUSPEND: the hook makes the hardware safe, the registry owns the flag (set even when the hook failed: a half-safe
// device refuses writes).
static err_h op_suspend(uint32_t i, const uint8_t* args, uint8_t* out, uint32_t out_cap, uint32_t* out_len) {
  sys_device_base_t* base = s_reg.devs[i].ctx;
  const bool was = base->suspended;
  const err_h err = s_reg.devs[i].type->suspend(base);
  base->suspended = true;
  if (!was) {
    state_changed(base->id, true);
  }
  return err;
}

// RESUME: only while suspended; a failing hook leaves the device suspended.
static err_h op_resume(uint32_t i, const uint8_t* args, uint8_t* out, uint32_t out_cap, uint32_t* out_len) {
  sys_device_base_t* base = s_reg.devs[i].ctx;
  if (!base->suspended) {
    return NULL;
  }
  const err_h err = s_reg.devs[i].type->resume(base);
  if (err == NULL) {
    base->suspended = false;
    state_changed(base->id, false);
  }
  return err;
}

// Refused while another device uses it. Otherwise the hardware is released, the instance unregistered and freed, even
// if uninstall() failed.
static err_h op_remove(uint32_t i, const uint8_t* args, uint8_t* out, uint32_t out_cap, uint32_t* out_len) {
  const dev_t_ d = s_reg.devs[i];
  const uint32_t user = find_user(d.id);
  ERR_CHECK(user == s_reg.dev_count, ERR_DEV_IN_USE, .id = d.id, .user = s_reg.devs[user].id);
  err_h err = d.type->uninstall(d.ctx);
  taskENTER_CRITICAL(&s_table_mux);
  const uint32_t j = find_dev(d.id);  // re-found: uninstall called other devices
  // keeps the creation order
  memmove(&s_reg.devs[j], &s_reg.devs[j + 1], (s_reg.dev_count - j - 1) * sizeof(s_reg.devs[0]));
  s_reg.dev_count--;
  taskEXIT_CRITICAL(&s_table_mux);
  free(d.ctx);
  return err;
}

static err_h op_info(uint32_t i, const uint8_t* args, uint8_t* out, uint32_t out_cap, uint32_t* out_len) {
  const sys_device_info_t info = {.type_id = s_reg.devs[i].type->type, .suspended = s_reg.devs[i].ctx->suspended};
  return cmd_reply(out, out_cap, out_len, &info, sizeof(info));
}

static err_h op_ops(uint32_t i, const uint8_t* args, uint8_t* out, uint32_t out_cap, uint32_t* out_len);

// Any action ID (0 = none; it may be recorded later), importance NONE..CRITICAL. Allowed while suspended: it
// configures the registry, not the device.
static err_h op_set_policy(uint32_t i, const uint8_t* args, uint8_t* out, uint32_t out_cap, uint32_t* out_len) {
  sys_device_error_policy_t p;
  memcpy(&p, args, sizeof(p));
  SYS_DEV_CHECK_IN_RANGE(s_reg.devs[i].id, p.importance, SYS_DEVICE_IMPORTANCE_NONE, SYS_DEVICE_IMPORTANCE_CRITICAL);
  taskENTER_CRITICAL(&s_table_mux);
  s_reg.devs[i].policy = p;
  taskEXIT_CRITICAL(&s_table_mux);
  return NULL;
}

static err_h op_get_policy(uint32_t i, const uint8_t* args, uint8_t* out, uint32_t out_cap, uint32_t* out_len) {
  return cmd_reply(out, out_cap, out_len, &s_reg.devs[i].policy, sizeof(s_reg.devs[i].policy));
}

static const struct {
  uint16_t op;
  uint16_t args_size;
  err_h (*fn)(uint32_t i, const uint8_t* args, uint8_t* out, uint32_t out_cap, uint32_t* out_len);
} k_registry_ops[] = {
    {SYS_DEVICE_OP_SUSPEND, 0, op_suspend},
    {SYS_DEVICE_OP_RESUME, 0, op_resume},
    {SYS_DEVICE_OP_REMOVE, 0, op_remove},
    {SYS_DEVICE_OP_INFO, 0, op_info},
    {SYS_DEVICE_OP_OPS, 0, op_ops},
    {SYS_DEVICE_OP_SET_ERROR_POLICY, sizeof(sys_device_error_policy_t), op_set_policy},
    {SYS_DEVICE_OP_GET_ERROR_POLICY, 0, op_get_policy},
};

// OPS: the registry's ops, then the type's table.
static err_h op_ops(uint32_t i, const uint8_t* args, uint8_t* out, uint32_t out_cap, uint32_t* out_len) {
  const sys_device_type_t* t = s_reg.devs[i].type;
  const uint32_t n = ARRAY_LEN(k_registry_ops);
  const uint32_t count = n + t->op_count;
  ERR_CHECK(count * 2 <= out_cap, ERR_CMD_OUT_FULL, .need = (uint16_t)(count * 2), .cap = (uint16_t)out_cap);
  for (uint32_t k = 0; k < count; k++) {
    const uint16_t op = k < n ? k_registry_ops[k].op : t->ops[k - n].op;
    memcpy(out + k * 2, &op, sizeof(op));  // little-endian target
  }
  *out_len = count * 2;
  return NULL;
}

// Any op but CREATE, on a registered device: the registry's, else the type's.
static err_h run_op(uint32_t i, uint16_t op, const uint8_t* args, uint32_t args_len, uint8_t* out, uint32_t out_cap,
                    uint32_t* out_len) {
  for (uint32_t k = 0; k < ARRAY_LEN(k_registry_ops); k++) {
    if (k_registry_ops[k].op == op) {
      const uint16_t need = k_registry_ops[k].args_size;
      ERR_CHECK(args_len == need, ERR_CMD_BAD_LEN, .got = (uint16_t)args_len, .need = need);
      return k_registry_ops[k].fn(i, args, out, out_cap, out_len);
    }
  }
  const cmd_f fn = type_op(s_reg.devs[i].type, op);
  ERR_CHECK(fn != NULL, ERR_DEV_UNKNOWN_OP, .op = op);
  return fn(s_reg.devs[i].ctx, args, args_len, out, out_cap, out_len);
}

/* ---- Types ---- */

// Constructors run one at a time before the scheduler: no lock. A misdefined type stops the boot, like a duplicate
// error module (err_register_module).
void sys_device_type_register(const sys_device_type_t* type) {
  assert(type != NULL && type->create != NULL && type->uninstall != NULL);
  assert(type->ctx_size >= sizeof(sys_device_base_t) && "the device's struct starts with sys_device_base_t");
  assert(type->suspend != NULL && type->resume != NULL && "a device type must have a SUSPEND and a RESUME hook");
  for (uint32_t k = 0; k < ARRAY_LEN(k_registry_ops); k++) {
    assert(type_op(type, k_registry_ops[k].op) == NULL &&
           "a registry op in a type's table (SUSPEND / RESUME are the type's hooks, the rest the registry's)");
  }
  assert(find_type(type->type) == NULL && "device type registered twice");
  if (s_reg.type_count >= CONFIG_SYS_DEVICE_TYPES) {
    assert(!"more device types than SYS_DEVICE_TYPES");
    return;
  }
  s_reg.types[s_reg.type_count++] = type;
}

static err_h wrap_op(err_h err, uint16_t op) {
  return err != NULL ? ERR_WRAP(err, ERR_DEV_OP, .op = op) : NULL;
}

/* One call, the args in place (no copy, so no size limit besides the caller's). Errors are wrapped in ERR_DEV_ID. */
static err_h call(uint16_t id, uint16_t op, const uint8_t* args, uint32_t args_len, uint8_t* out, uint32_t out_cap,
                  uint32_t* out_len) {
  // SUSPEND / RESUME / RESET to "no device" = every device (sys_device_sweep). Not wrapped in ERR_DEV_ID{NONE}: the
  // sweep's own error already names the device that failed, and the error policy reads that ID.
  if (id == SYS_DEVICE_ID_NONE &&
      (op == SYS_DEVICE_OP_SUSPEND || op == SYS_DEVICE_OP_RESUME || op == SYS_DEVICE_OP_RESET)) {
    ERR_CHECK(args_len == 0, ERR_CMD_BAD_LEN, .got = (uint16_t)args_len, .need = 0);
    return sys_device_sweep(op);
  }
  err_h err = NULL;
  LOCK();
  const uint32_t i = find_dev(id);
  const bool found = i != s_reg.dev_count;  // before the op: REMOVE shrinks the table
  if (op == SYS_DEVICE_OP_CREATE) {
    if (id == SYS_DEVICE_ID_NONE) {
      err = ERR_NEW(ERR_DEV_BAD_ID, .id = id);
    } else if (found) {
      err = ERR_NEW(ERR_DEV_ID_TAKEN, .id = id);
    } else if (s_reg.dev_count == CONFIG_SYS_DEVICE_MAX) {
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

err_h sys_device_set_guard(uint8_t contract, sys_device_guard_f fn) {
  ERR_CHECK(fn != NULL && contract != SYS_DEVICE_CONTRACT_DEVICE, ERR_DEV_BAD_ARG);
  for (uint32_t i = 0; i < s_reg.guard_count; i++) {
    ERR_CHECK(s_reg.guards[i].contract != contract, ERR_DEV_BAD_ARG);  // one guard per contract
  }
  ERR_CHECK(s_reg.guard_count < CONFIG_SYS_DEVICE_GUARDS, ERR_DEV_BAD_ARG);
  s_reg.guards[s_reg.guard_count++] = (guard_t){.contract = contract, .fn = fn};
  return NULL;
}

static sys_device_guard_f find_guard(uint16_t op) {
  for (uint32_t i = 0; i < s_reg.guard_count; i++) {
    if (s_reg.guards[i].contract == (uint8_t)(op >> 8)) {
      return s_reg.guards[i].fn;
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

err_h sys_device_read(uint16_t id, uint16_t op, const void* args, uint32_t args_len, void* out, uint32_t out_size) {
  uint32_t out_len = 0;
  ERR_TRY(sys_device_call(id, op, args, args_len, out, out_size, &out_len));
  ERR_CHECK(out_len == out_size, ERR_CMD_BAD_LEN, .got = (uint16_t)out_len, .need = (uint16_t)out_size);
  return NULL;
}

err_h sys_device_call_direct(uint16_t id, uint16_t op, const void* args, uint32_t args_len, void* out,
                             uint32_t out_cap, uint32_t* out_len) {
  return call_checked(false, id, op, args, args_len, out, out_cap, out_len);
}

/* ----------------------------------------------------------------------------
 * Sweeps: best effort, the first error is returned, the rest reported
 * ------------------------------------------------------------------------- */

/* SUSPEND, RESET and REMOVE go newest first (users before the devices they use: a driver makes its outputs safe before
 * the pins it drives go idle), RESUME oldest first. REMOVE leaves the board's own devices (sys_device_mark_system). */
err_h sys_device_sweep(uint16_t op) {
  ERR_CHECK(op == SYS_DEVICE_OP_SUSPEND || op == SYS_DEVICE_OP_RESUME || op == SYS_DEVICE_OP_RESET ||
                op == SYS_DEVICE_OP_REMOVE,
            ERR_DEV_UNKNOWN_OP, .op = op);
  const bool newest_first = op != SYS_DEVICE_OP_RESUME;
  err_h first = NULL;
  LOCK();
  // A snapshot of the IDs: REMOVE changes the list while we walk it.
  uint16_t ids[CONFIG_SYS_DEVICE_MAX];
  uint32_t n = 0;
  for (uint32_t i = 0; i < s_reg.dev_count; i++) {
    const dev_t_* d = &s_reg.devs[newest_first ? s_reg.dev_count - 1 - i : i];
    if (!(op == SYS_DEVICE_OP_REMOVE && d->system)) {
      ids[n++] = d->id;
    }
  }
  const bool outer = s_reg.sweep_depth++ == 0;
  if (outer) {
    s_reg.sweep_changed = false;
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
  s_reg.sweep_depth--;
  if (outer && s_reg.sweep_changed && s_reg.state_listener != NULL) {
    s_reg.state_listener(SYS_DEVICE_ID_NONE, op == SYS_DEVICE_OP_SUSPEND);
  }
  UNLOCK();
  return first;
}

void sys_device_mark_system(void) {
  LOCK();
  taskENTER_CRITICAL(&s_table_mux);
  for (uint32_t i = 0; i < s_reg.dev_count; i++) {
    s_reg.devs[i].system = true;
  }
  taskEXIT_CRITICAL(&s_table_mux);
  UNLOCK();
}
