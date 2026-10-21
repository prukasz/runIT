#include "sys_device.h"
#include "sys_device_policy_internal.h"

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
  bool system;  // the board's own (sys_device_mark_system): never removed
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

/* One recursive lock for the registry and for every call into a device (sys_device.h "Locking"). */
U_RECURSIVE_MUTEX_DEFINE(sys_device_mutex);

#define LOCK() U_RECURSIVE_MUTEX_LOCK(sys_device_mutex, WAIT_FOREVER)
#define UNLOCK() U_RECURSIVE_MUTEX_UNLOCK(sys_device_mutex)

/* ---- Lookups (lock held) ---- */

static const sys_device_type_t* find_type(uint16_t type) {
  for (uint32_t i = 0; i < s_reg.type_count; i++) {
    if (s_reg.types[i]->type == type) {
      return s_reg.types[i];
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
  // newest last
  s_reg.devs[s_reg.dev_count++] = (dev_t_){.id = id, .type = type, .ctx = ctx};
  sys_device_policy_add(id);
  return NULL;
}

/* ---- State listener ---- */

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
 * The registry's ops: the same on every device, never in a type's table. Written like a driver's (OP_READ / OP_WRITE,
 * a sys_device_op_t table), but their ctx is the instance's entry (dev_t_, lock held), not the device's struct.
 * ------------------------------------------------------------------------- */

// SUSPEND: the hook makes the hardware safe, the registry owns the flag (set even when the hook failed: a half-safe
// device refuses writes).
OP_WRITE(SYS_DEVICE, SUSPEND) {
  const dev_t_* d = ctx;
  const bool was = d->ctx->suspended;
  const err_h err = d->type->suspend(d->ctx);
  d->ctx->suspended = true;
  if (!was) {
    state_changed(d->id, true);
  }
  return err;
}

// RESUME: only while suspended; a failing hook leaves the device suspended.
OP_WRITE(SYS_DEVICE, RESUME) {
  const dev_t_* d = ctx;
  if (!d->ctx->suspended) {
    return NULL;
  }
  const err_h err = d->type->resume(d->ctx);
  if (err == NULL) {
    d->ctx->suspended = false;
    state_changed(d->id, false);
  }
  return err;
}

// Refused for a board device and while another device uses it. Otherwise the hardware is released, the instance
// unregistered and freed, even if uninstall() failed.
OP_WRITE(SYS_DEVICE, REMOVE) {
  const dev_t_ d = *(const dev_t_*)ctx;  // a copy: the entry moves when the table shrinks
  ERR_CHECK(!d.system, ERR_DEV_SYSTEM, .id = d.id);
  const uint32_t user = find_user(d.id);
  ERR_CHECK(user == s_reg.dev_count, ERR_DEV_IN_USE, .id = d.id, .user = s_reg.devs[user].id);
  err_h err = d.type->uninstall(d.ctx);
  const uint32_t j = find_dev(d.id);  // re-found: uninstall called other devices
  // keeps the creation order
  memmove(&s_reg.devs[j], &s_reg.devs[j + 1], (s_reg.dev_count - j - 1) * sizeof(s_reg.devs[0]));
  s_reg.dev_count--;
  sys_device_policy_remove(d.id);
  free(d.ctx);
  return err;
}

OP_READ(SYS_DEVICE, INFO) {
  const dev_t_* d = ctx;
  const sys_device_info_t info = {.type_id = d->type->type, .suspended = d->ctx->suspended};
  return cmd_reply(out, out_cap, out_len, &info, sizeof(info));
}

// ERROR_POLICY: the policy is kept and checked in sys_device_policy.c. Allowed while suspended: it configures the
// registry, not the device.
OP_WRITE(SYS_DEVICE, ERROR_POLICY) {
  return sys_device_policy_set(((const dev_t_*)ctx)->id, args);
}

OP_READ(SYS_DEVICE, ERROR_POLICY) {
  return sys_device_policy_reply(((const dev_t_*)ctx)->id, out, out_cap, out_len);
}

static err_h SYS_DEVICE_r_OPS(void* ctx, const uint8_t* in, uint32_t in_len, uint8_t* out, uint32_t out_cap,
                              uint32_t* out_len);

static const sys_device_op_t k_registry_ops[] = {
    OP_ENTRY_W(SYS_DEVICE, SUSPEND),  OP_ENTRY_W(SYS_DEVICE, RESUME),  OP_ENTRY_W(SYS_DEVICE, REMOVE),
    OP_ENTRY_R(SYS_DEVICE, INFO),     OP_ENTRY_R(SYS_DEVICE, OPS),     OP_ENTRY(SYS_DEVICE, ERROR_POLICY),
};

// The row of `op` in a table, or NULL.
static const sys_device_op_t* find_op(const sys_device_op_t* ops, uint32_t count, uint16_t op) {
  for (uint32_t i = 0; i < count; i++) {
    if (ops[i].op == op) {
      return &ops[i];
    }
  }
  return NULL;
}

// The device runs this op of its type's table (has_op: not every wiring gives every op).
static bool device_has_op(const dev_t_* d, uint16_t op) {
  return d->type->has_op == NULL || d->type->has_op(d->ctx, op);
}

#define SIDE_READ  0x01u  // OPS: the op has a read side
#define SIDE_WRITE 0x02u  // ... a write side

// OPS rows of a table: [op u16][sides u8] per op (with `wired`: per op the device runs, has_op).
static err_h append_ops(const dev_t_* d, const sys_device_op_t* ops, uint32_t count, bool wired, uint8_t* out,
                        uint32_t out_cap, uint32_t* pos) {
  for (uint32_t k = 0; k < count; k++) {
    if (wired && !device_has_op(d, ops[k].op)) continue;
    const uint8_t sides = (ops[k].read != NULL ? SIDE_READ : 0) | (ops[k].write != NULL ? SIDE_WRITE : 0);
    ERR_TRY(cmd_append(out, out_cap, pos, &ops[k].op, sizeof(uint16_t)));  // little-endian target
    ERR_TRY(cmd_append(out, out_cap, pos, &sides, sizeof(sides)));
  }
  return NULL;
}

// OPS: the registry's ops, then the type's table (the ops this device runs).
OP_READ(SYS_DEVICE, OPS) {
  const dev_t_* d = ctx;
  uint32_t pos = 0;
  ERR_TRY(append_ops(d, k_registry_ops, ARRAY_LEN(k_registry_ops), false, out, out_cap, &pos));
  ERR_TRY(append_ops(d, d->type->ops, d->type->op_count, true, out, out_cap, &pos));
  *out_len = pos;
  return NULL;
}

// Any op but CREATE, on a registered device: the registry's (ctx = the entry), else the type's (ctx = its struct).
static err_h run_op(uint32_t i, uint8_t dir, uint16_t op, const uint8_t* args, uint32_t args_len, uint8_t* out,
                    uint32_t out_cap, uint32_t* out_len) {
  dev_t_* d = &s_reg.devs[i];
  const sys_device_op_t* row = find_op(k_registry_ops, ARRAY_LEN(k_registry_ops), op);
  void* ctx = d;
  if (row == NULL) {
    row = find_op(d->type->ops, d->type->op_count, op);
    ERR_CHECK(row != NULL && device_has_op(d, op), ERR_DEV_UNKNOWN_OP, .op = op);
    ctx = d->ctx;
  }
  cmd_f fn = NULL;
  ERR_TRY(cmd_side(row->read, row->write, dir, &fn));
  // A suspended device takes no write of its type's table but the ones marked always (an event, a pin's owner).
  if (ctx == d->ctx && dir == CMD_DIR_WRITE && !row->always) {
    ERR_TRY(sys_device_check_active(d->ctx));
  }
  return fn(ctx, args, args_len, out, out_cap, out_len);
}

/* ---- Types ---- */

// Constructors run one at a time before the scheduler: no lock. A misdefined type stops the boot, like a duplicate
// error module (err_register_module).
void sys_device_type_register(const sys_device_type_t* type) {
  assert(type != NULL && type->create != NULL && type->uninstall != NULL);
  assert(type->ctx_size >= sizeof(sys_device_base_t) && "the device's struct starts with sys_device_base_t");
  assert(type->suspend != NULL && type->resume != NULL && "a device type must have a SUSPEND and a RESUME hook");
  for (uint32_t k = 0; k < ARRAY_LEN(k_registry_ops); k++) {
    assert(find_op(type->ops, type->op_count, k_registry_ops[k].op) == NULL &&
           "a registry op in a type's table (SUSPEND / RESUME are the type's hooks, the rest the registry's)");
  }
  assert(find_type(type->type) == NULL && "device type registered twice");
  if (s_reg.type_count >= CONFIG_SYS_DEVICE_TYPES) {
    assert(!"more device types than SYS_DEVICE_TYPES");
    return;
  }
  s_reg.types[s_reg.type_count++] = type;
}

static err_h wrap_op(err_h err, uint8_t dir, uint16_t op) {
  return err != NULL ? ERR_WRAP(err, ERR_DEV_OP, .op = op, .dir = dir) : NULL;
}

/* One call, the args in place (no copy, so no size limit besides the caller's). Errors are wrapped in ERR_DEV_ID. */
static err_h call(uint16_t id, uint8_t dir, uint16_t op, const uint8_t* args, uint32_t args_len, uint8_t* out,
                  uint32_t out_cap, uint32_t* out_len) {
  // SUSPEND / RESUME / RESET to "no device" = every device (sys_device_sweep). Not wrapped in ERR_DEV_ID{NONE}: the
  // sweep's own error already names the device that failed, and the error policy reads that ID.
  if (id == SYS_DEVICE_ID_NONE &&
      (op == SYS_DEVICE_OP_SUSPEND || op == SYS_DEVICE_OP_RESUME || op == SYS_DEVICE_OP_RESET)) {
    ERR_CHECK(dir == CMD_DIR_WRITE, ERR_CMD_DIRECTION, .dir = dir);
    ERR_CHECK(args_len == 0, ERR_CMD_BAD_LEN, .got = (uint16_t)args_len, .need = 0);
    return sys_device_sweep(op);
  }
  err_h err = NULL;
  LOCK();
  const uint32_t i = find_dev(id);
  const bool found = i != s_reg.dev_count;  // before the op: REMOVE shrinks the table
  if (op == SYS_DEVICE_OP_CREATE) {
    if (dir != CMD_DIR_WRITE) {
      err = ERR_NEW(ERR_CMD_DIRECTION, .dir = dir);
    } else if (id == SYS_DEVICE_ID_NONE) {
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
    err = run_op(i, dir, op, args, args_len, out, out_cap, out_len);
  }
  UNLOCK();
  if (op == SYS_DEVICE_OP_CREATE || found) {
    err = wrap_op(err, dir, op);  // not around NOT_FOUND: there was no op to run
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

static err_h call_checked(bool guarded, uint16_t id, uint8_t dir, uint16_t op, const void* args, uint32_t args_len,
                          void* out, uint32_t out_cap, uint32_t* out_len) {
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
    err_h err = guard(id, dir, op, args, args_len, &handled);
    if (handled) {
      if (err != NULL && err->tag != ERR_DEV_ID) {
        err = ERR_WRAP(wrap_op(err, dir, op), ERR_DEV_ID, .id = id);
      }
      return err;
    }
    err_free(err);  // a guard that passes the op on returns nothing
  }
  return call(id, dir, op, args, args_len, out, out_cap, out_len);
}

err_h sys_device_call(uint16_t id, uint8_t dir, uint16_t op, const void* args, uint32_t args_len, void* out,
                      uint32_t out_cap, uint32_t* out_len) {
  return call_checked(true, id, dir, op, args, args_len, out, out_cap, out_len);
}

err_h sys_device_write(uint16_t id, uint16_t op, const void* args, uint32_t args_len) {
  return sys_device_call(id, CMD_DIR_WRITE, op, args, args_len, NULL, 0, NULL);
}

err_h sys_device_read(uint16_t id, uint16_t op, const void* args, uint32_t args_len, void* out, uint32_t out_size) {
  uint32_t out_len = 0;
  ERR_TRY(sys_device_call(id, CMD_DIR_READ, op, args, args_len, out, out_size, &out_len));
  ERR_CHECK(out_len == out_size, ERR_CMD_BAD_LEN, .got = (uint16_t)out_len, .need = (uint16_t)out_size);
  return NULL;
}

err_h sys_device_call_direct(uint16_t id, uint8_t dir, uint16_t op, const void* args, uint32_t args_len, void* out,
                             uint32_t out_cap, uint32_t* out_len) {
  return call_checked(false, id, dir, op, args, args_len, out, out_cap, out_len);
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
    err_h err = sys_device_write(ids[i], op, NULL, 0);
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
  for (uint32_t i = 0; i < s_reg.dev_count; i++) {
    s_reg.devs[i].system = true;
  }
  UNLOCK();
}
