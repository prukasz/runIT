#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "sys_device_driver.h"
#include "sys_device_registry_internal.h"
#include "sys_device_slots_internal.h"
#include "utils.h"

ERR_MODULE_DEFINE(MODULE_ID_SYS_DEVICE, ERR_SYS_DEVICE_MAP)

/* The registry's two contract IDs: the device (its lifecycle ops) and a device's own ops, which no contract takes. */
__attribute__((used)) const uint8_t sys_device__contract_0xFF = 0;

/* ----------------------------------------------------------------------------
 * Op tables
 * ------------------------------------------------------------------------- */

static const sys_device_op_t* find_row(const sys_device_op_t* ops, uint32_t count, uint16_t op) {
  for (uint32_t i = 0; i < count; i++) {
    if (ops[i].op == op) {
      return &ops[i];
    }
  }
  return NULL;
}

const sys_device_op_t* sys_device_type_op(uint8_t id, uint16_t op) {
  const sys_device_type_t* type = sys_device_slot_type(id);
  if (type == NULL) {
    return NULL;
  }
  const sys_device_op_t* row = find_row(type->ops, type->op_count, op);
  if (row == NULL || sys_device_slot_is_remote(id) || type->has_op == NULL) {
    return row;  // a remote device's wiring is its board's
  }
  return type->has_op(sys_device_slot_instance(id), op) ? row : NULL;
}

/* ----------------------------------------------------------------------------
 * The registry's ops: written like a driver's (OP_DECLARE_READ / OP_DECLARE_WRITE, a sys_device_op_t table), but their ctx is the
 * device's ID, and the slot's lock is held.
 * ------------------------------------------------------------------------- */

#define SYS_DEVICE_CTX void  // not a struct: the ID as a number
#define CTX_ID         ((uint8_t)(uintptr_t)c)

#define SIDE_READ  0x01u  // OPS: the op has a read side
#define SIDE_WRITE 0x02u  // ... a write side

/* A remote device runs none of its type's hooks here: its hardware is its board's. SUSPEND / RESUME change only the
 * flag (this board stops writing to it), REMOVE frees only where it is. */

// SUSPEND: the hook makes the hardware safe, the registry owns the flag (set even when the hook failed: a half-safe
// device refuses writes).
OP_DECLARE_WRITE(CONTRACT_SYS_DEVICE, OP_SUSPEND) {
  const uint8_t id = CTX_ID;
  err_h err = sys_device_slot_is_remote(id) ? NULL : sys_device_slot_type(id)->suspend(sys_device_slot_instance(id));
  ERR_KEEP_FIRST(err, sys_device_slot_set_suspended(id, true));
  return err;
}

// RESUME: only while suspended; a failing hook leaves the device suspended.
OP_DECLARE_WRITE(CONTRACT_SYS_DEVICE, OP_RESUME) {
  const uint8_t id = CTX_ID;
  if (!sys_device_slot_is_suspended(id)) {
    return NULL;
  }
  if (!sys_device_slot_is_remote(id)) {
    ERR_TRY(sys_device_slot_type(id)->resume(sys_device_slot_instance(id)));
  }
  return sys_device_slot_set_suspended(id, false);
}

// Gives back what the device took (sys_device_res_add), the last added first. Every resource is released; the first
// error is returned. The slot's lock is held and the driver's struct is still there.
static err_h release_resources(uint8_t id) {
  err_h err = NULL;
  sys_device_res_t* res = sys_device_slot_res_pop(id);
  while (res != NULL) {
    const sys_device_res_f release = res->release;
    res->release = NULL;  // not added any more
    ERR_KEEP_FIRST(err, release(res->arg));
    res = sys_device_slot_res_pop(id);
  }
  return err;
}

err_h sys_device_res_add(uint8_t owner, sys_device_res_t* res, sys_device_res_f release, void* arg) {
  ERR_CHECK(res != NULL && release != NULL && res->release == NULL, ERR_DEV_BAD_ARG);
  res->release = release;
  res->arg = arg;
  const err_h err = sys_device_slot_res_push(owner, res);
  if (err != NULL) {
    res->release = NULL;
  }
  return err;
}

// REMOVE: refused for a board device and while other devices use it (sys_device_use). Otherwise the NOLOCK calls in flight are waited
// for (the lock held, no new one starts), a device not suspended yet goes into its safe state (the type's suspend(),
// its last state), its resources are released and the slot freed, even if a step failed.
OP_DECLARE_WRITE(CONTRACT_SYS_DEVICE, OP_REMOVE) {
  const uint8_t id = CTX_ID;
  ERR_CHECK(!sys_device_slot_is_system(id), ERR_DEV_SYSTEM, .id = id);
  uint64_t users = 0;  // checked and marked as being removed in one step: no device declares a use of it afterwards
  ERR_CHECK(sys_device_slot_begin_remove(id, &users), ERR_DEV_IN_USE, .id = id, .users = users);
  sys_device_slot_drain(id);
  void* instance = sys_device_slot_instance(id);
  err_h err = NULL;
  if (!sys_device_slot_is_remote(id)) {
    if (!sys_device_slot_is_suspended(id)) {
      err = sys_device_slot_type(id)->suspend(instance);
    }
    ERR_KEEP_FIRST(err, release_resources(id));
  }
  ERR_KEEP_FIRST(err, sys_device_slot_uninstall(id));
  free(instance);
  return err;
}

OP_DECLARE_READ(CONTRACT_SYS_DEVICE, OP_INFO) {
  const uint8_t id = CTX_ID;
  const sys_device_info_t info = {.type_id = sys_device_type_id(sys_device_slot_type(id)),
                                  .suspended = sys_device_slot_is_suspended(id)};
  return cmd_reply(out, &info, sizeof(info));
}

static err_h CONTRACT_SYS_DEVICE_r_OP_OPS(CMD_F_PARAMS);  // lists the table below, itself in it: defined after it

static const sys_device_op_t k_registry_ops[] = {
    OP_ENTRY_W(CONTRACT_SYS_DEVICE, OP_SUSPEND), OP_ENTRY_W(CONTRACT_SYS_DEVICE, OP_RESUME), OP_ENTRY_W(CONTRACT_SYS_DEVICE, OP_REMOVE),
    OP_ENTRY_R(CONTRACT_SYS_DEVICE, OP_INFO),    OP_ENTRY_R(CONTRACT_SYS_DEVICE, OP_OPS),
};

// An OPS row: [op u16][sides u8], for an op the device answers as a command (a local op is left out).
static err_h append_op(const sys_device_op_t* row, cmd_out_t* out) {
  if ((row->flags & SYS_DEVICE_OP_LOCAL) != 0u) {
    return NULL;
  }
  const uint8_t sides = (row->read != NULL ? SIDE_READ : 0u) | (row->write != NULL ? SIDE_WRITE : 0u);
  ERR_TRY(cmd_append(out, &row->op, sizeof(row->op)));  // little-endian target
  return cmd_append(out, &sides, sizeof(sides));
}

// OPS: the registry's ops, then the ones of the type this device runs (sys_device_type_op). The answer is a list, built
// with cmd_append.
OP_DECLARE_READ(CONTRACT_SYS_DEVICE, OP_OPS) {
  const uint8_t id = CTX_ID;
  const sys_device_type_t* type = sys_device_slot_type(id);
  for (uint32_t i = 0; i < ARRAY_LEN(k_registry_ops); i++) {
    ERR_TRY(append_op(&k_registry_ops[i], out));
  }
  for (uint16_t i = 0; i < type->op_count; i++) {
    if (sys_device_type_op(id, type->ops[i].op) != NULL) {
      ERR_TRY(append_op(&type->ops[i], out));
    }
  }
  return NULL;
}

const sys_device_op_t* sys_device_registry_op(uint16_t op) {
  return find_row(k_registry_ops, ARRAY_LEN(k_registry_ops), op);
}

/* ----------------------------------------------------------------------------
 * Types: registered before app_main (SYS_DEVICE_TYPE_DEFINE), read without a lock afterwards
 * ------------------------------------------------------------------------- */

/** @brief A registered type and its ID: the ID is the driver's (SYS_DEVICE_TYPE_DECLARE), not a field of the type. */
typedef struct {
  uint16_t id;
  const sys_device_type_t* type;
} type_entry_t;

static type_entry_t s_types[CONFIG_SYS_DEVICE_TYPES];
static uint32_t s_type_count;

const sys_device_type_t* sys_device_type_find(uint16_t id) {
  for (uint32_t i = 0; i < s_type_count; i++) {
    if (s_types[i].id == id) {
      return s_types[i].type;
    }
  }
  return NULL;
}

uint16_t sys_device_type_id(const sys_device_type_t* type) {
  for (uint32_t i = 0; i < s_type_count; i++) {
    if (s_types[i].type == type) {
      return s_types[i].id;
    }
  }
  return 0;
}

void sys_device_type_register(uint16_t id, const sys_device_type_t* type) {
  assert(id >= SYS_DEVICE_TYPE_FIRST && "a driver's type ID starts at SYS_DEVICE_TYPE_FIRST");
  assert(type != NULL && type->create != NULL);
  assert(type->suspend != NULL && type->resume != NULL && "a device type must have a SUSPEND and a RESUME hook");
  for (uint32_t i = 0; i < ARRAY_LEN(k_registry_ops); i++) {
    assert(find_row(type->ops, type->op_count, k_registry_ops[i].op) == NULL &&
           "a registry op in a type's table (SUSPEND / RESUME are the type's hooks, the rest the registry's)");
  }
  assert(sys_device_type_find(id) == NULL && "device type registered twice");
  assert(s_type_count < CONFIG_SYS_DEVICE_TYPES && "more device types than SYS_DEVICE_TYPES");
  if (s_type_count < CONFIG_SYS_DEVICE_TYPES) {
    s_types[s_type_count++] = (type_entry_t){.id = id, .type = type};
  }
}

/* ----------------------------------------------------------------------------
 * CREATE: not a row of the table above, it has no device yet (CREATE_REMOTE: sys_device_remote.c)
 * ------------------------------------------------------------------------- */

err_h sys_device_registry_create(uint8_t id, cmd_in_t in) {
  ERR_CHECK(in.len >= SYS_DEVICE_CREATE_HDR, ERR_CMD_BAD_LEN, .got = (uint16_t)in.len, .need = SYS_DEVICE_CREATE_HDR);
  uint16_t type_id;
  memcpy(&type_id, in.data, sizeof(type_id));  // little-endian target
  const uint8_t flags = in.data[sizeof(type_id)];
  ERR_CHECK((flags & ~SYS_DEVICE_CREATE_SYSTEM) == 0, ERR_DEV_BAD_ARG);
  const sys_device_type_t* type = sys_device_type_find(type_id);
  ERR_CHECK(type != NULL, ERR_DEV_TYPE_UNKNOWN, .type = type_id);
  ERR_CHECK(in.len - SYS_DEVICE_CREATE_HDR == type->cfg_size, ERR_CMD_BAD_LEN,
            .got = (uint16_t)(in.len - SYS_DEVICE_CREATE_HDR), .need = type->cfg_size);
  ERR_TRY(sys_device_slot_begin_create(id));  // the new generation, before create() hands anything out by it

  void* instance = calloc(1, type->ctx_size);
  ERR_CHECK(instance != NULL || type->ctx_size == 0, ERR_DEV_NO_MEM, .id = id, .size = type->ctx_size);
  err_h err = type->create(id, instance, in.data + SYS_DEVICE_CREATE_HDR);
  if (err == NULL) {
    err = sys_device_slot_install(id, type, instance, (flags & SYS_DEVICE_CREATE_SYSTEM) != 0, false);
  }
  if (err != NULL) {
    ERR_KEEP_FIRST(err, release_resources(id));  // what create() took so far; the cause stays first
    err_free(sys_device_slot_uninstall(id));    // the devices create() declared it uses are free of it
    free(instance);
    return ERR_WRAP(err, ERR_DEV_INSTALL, .id = id, .type = type_id);
  }
  return NULL;
}

uint8_t sys_device_gen(uint8_t id) {
  return sys_device_slot_gen(id);
}

/* ----------------------------------------------------------------------------
 * Dependencies
 * ------------------------------------------------------------------------- */

err_h sys_device_use(uint8_t user, uint8_t dep) {
  return sys_device_slot_use(user, dep);  // the caller is in create(): it holds the lock of `user`
}

err_h sys_device_require_op(uint8_t user, uint8_t dep, uint16_t op, uint8_t dir) {
  ERR_TRY(sys_device_use(user, dep));
  const sys_device_op_t* row = sys_device_type_op(dep, op);  // a snapshot: `dep` can't go while `user` uses it
  ERR_CHECK(sys_device_op_side(row, dir) != NULL, ERR_DEV_UNKNOWN_OP, .op = op);
  return NULL;
}
