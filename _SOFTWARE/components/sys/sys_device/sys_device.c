#include "sys_device.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
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
  uint8_t dep_count;
  uint16_t deps[CONFIG_SYS_DEVICE_MAX_DEPS];  // the devices this one uses (sys_device_use), each once
  const sys_device_type_t* type;
  sys_device_base_t* ctx;
} dev_t_;

static const sys_device_type_t* s_types[CONFIG_SYS_DEVICE_TYPES];  // filled by constructors, before any task
static uint32_t s_type_count;

/* In creation order. A device can only use devices that exist when it is created, and a used device can't be
 * removed, so this order is a dependency order: the sweeps go newest first (SUSPEND, REMOVE) or oldest first
 * (RESUME), whatever the IDs. */
static dev_t_ s_devs[CONFIG_SYS_DEVICE_MAX];
static uint32_t s_dev_count;

// The device whose create() is running: sys_device_use() records into it. One at a time (the lock is held).
static dev_t_ s_new;
static bool s_creating;

/* One recursive lock for the registry and for every call into a device (sys_device.h "Locking"). */
U_RECURSIVE_MUTEX_DEFINE(sys_device_mutex);

#define LOCK() U_RECURSIVE_MUTEX_LOCK(sys_device_mutex, WAIT_FOREVER)
#define UNLOCK() U_RECURSIVE_MUTEX_UNLOCK(sys_device_mutex)

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
  assert(has_op(type, SYS_DEVICE_OP_SUSPEND) && has_op(type, SYS_DEVICE_OP_RESUME) &&
         "a device type must answer SUSPEND and RESUME");
  assert(!has_op(type, SYS_DEVICE_OP_REMOVE) && !has_op(type, SYS_DEVICE_OP_INFO) && !has_op(type, SYS_DEVICE_OP_OPS) &&
         "REMOVE / INFO / OPS are answered by the registry");
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

static bool uses(const dev_t_* d, uint16_t dep) {
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
    if (uses(&s_devs[i], id)) {
      return i;
    }
  }
  return s_dev_count;
}

err_h sys_device_use(uint16_t id, uint16_t dep) {
  err_h err = NULL;
  LOCK();  // recursive: create() runs under it
  if (!s_creating || s_new.id != id) {
    err = ERR_NEW(ERR_DEV_BAD_ARG);  // only from the device's own create()
  } else if (dep == id || find_dev(dep) == s_dev_count) {
    err = ERR_NEW(ERR_DEV_NOT_FOUND, .id = dep);
  } else if (!uses(&s_new, dep)) {
    if (s_new.dep_count == CONFIG_SYS_DEVICE_MAX_DEPS) {
      err = ERR_NEW(ERR_DEV_DEPS_FULL, .id = id);
    } else {
      s_new.deps[s_new.dep_count++] = dep;
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
  s_new = (dev_t_){.id = id, .type = type, .ctx = ctx};
  s_creating = true;
  err_h err = type->create(ctx, args + 2);
  s_creating = false;
  if (err != NULL) {
    SYS_DEVICE_KEEP_FIRST(err, type->uninstall(ctx));  // the cause stays; a rollback failure is dropped
    free(ctx);
    return ERR_WRAP(err, ERR_DEV_INSTALL, .id = id, .type = type_id);
  }
  s_devs[s_dev_count++] = s_new;  // newest last
  return NULL;
}

// Refused while another device uses it. Otherwise the hardware is released, the instance unregistered and
// freed, even if uninstall() failed.
static err_h remove_dev(uint16_t id) {
  const uint32_t user = find_user(id);
  ERR_CHECK(user == s_dev_count, ERR_DEV_IN_USE, .id = id, .user = s_devs[user].id);
  const dev_t_ d = s_devs[find_dev(id)];
  err_h err = d.type->uninstall(d.ctx);
  const uint32_t j = find_dev(id);  // re-found: uninstall called other devices
  memmove(&s_devs[j], &s_devs[j + 1], (s_dev_count - j - 1) * sizeof(s_devs[0]));  // keeps the creation order
  s_dev_count--;
  free(d.ctx);
  return err;
}

static err_h reply_info(const dev_t_* d, uint8_t* out, uint32_t out_cap, uint32_t* out_len) {
  const sys_device_info_t info = {.type_id = d->type->type, .suspended = d->ctx->suspended};
  return cmd_reply(out, out_cap, out_len, &info, sizeof(info));
}

// OPS: the generic ops, then the type's table.
static err_h reply_ops(const sys_device_type_t* t, uint8_t* out, uint32_t out_cap, uint32_t* out_len) {
  static const uint16_t k_generic[] = {SYS_DEVICE_OP_REMOVE, SYS_DEVICE_OP_INFO, SYS_DEVICE_OP_OPS};
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

// Any op but CREATE, on a registered device.
static err_h run_op(uint32_t index, uint16_t op, const uint8_t* args, uint32_t args_len, uint8_t* out,
                    uint32_t out_cap, uint32_t* out_len) {
  const dev_t_ d = s_devs[index];
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

/* One call, the args in place (no copy, so no size limit besides the caller's). Errors are wrapped in ERR_DEV_ID. */
static err_h call(uint16_t id, uint16_t op, const uint8_t* args, uint32_t args_len, uint8_t* out, uint32_t out_cap,
                  uint32_t* out_len) {
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

err_h sys_device_call(uint16_t id, uint16_t op, const void* args, uint32_t args_len, void* out, uint32_t out_cap,
                      uint32_t* out_len) {
  static const uint8_t k_no_args[1];  // handlers memcpy their args: never from NULL
  uint32_t dummy;
  ERR_CHECK(args != NULL || args_len == 0, ERR_DEV_BAD_ARG);
  if (out_len == NULL) {
    out_len = &dummy;
  }
  *out_len = 0;
  return call(id, op, args != NULL ? args : k_no_args, args_len, out, out_cap, out_len);
}

/* ----------------------------------------------------------------------------
 * Sweeps: best effort, the first error is returned, the rest reported
 * ------------------------------------------------------------------------- */

static err_h sweep(uint16_t op, bool newest_first) {
  err_h first = NULL;
  LOCK();
  // A snapshot of the IDs: REMOVE changes the list while we walk it.
  uint16_t ids[CONFIG_SYS_DEVICE_MAX];
  const uint32_t n = s_dev_count;
  for (uint32_t i = 0; i < n; i++) {
    ids[i] = s_devs[newest_first ? n - 1 - i : i].id;
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
  UNLOCK();
  return first;
}

// Users before the devices they use.
err_h sys_device_suspend_all(void) {
  return sweep(SYS_DEVICE_OP_SUSPEND, true);
}

// The used devices before their users.
err_h sys_device_resume_all(void) {
  return sweep(SYS_DEVICE_OP_RESUME, false);
}

err_h sys_device_remove_all(void) {
  return sweep(SYS_DEVICE_OP_REMOVE, true);
}
