#include <stdlib.h>
#include <string.h>

#include "sys_device_hooks.h"
#include "sys_device_registry_internal.h"
#include "sys_device_remote_internal.h"
#include "sys_device_slots_internal.h"

static sys_device_remote_f s_remote;  // set at boot, read without a lock afterwards

void sys_device_set_remote(sys_device_remote_f fn) {
  s_remote = fn;
}

// The local ID of the remote device at `where`, SYS_DEVICE_ID_NONE when it has none. A snapshot (no lock).
static uint8_t find_remote(const sys_device_remote_t* where) {
  for (uint8_t i = 0; i < SYS_DEVICE_COUNT; i++) {
    const sys_device_remote_t* r = sys_device_slot_is_remote(i) ? sys_device_slot_instance(i) : NULL;
    if (r != NULL && r->board == where->board && r->remote_id == where->remote_id) {
      return i;
    }
  }
  return SYS_DEVICE_ID_NONE;
}

err_h sys_device_remote_create(uint8_t id, cmd_in_t in) {
  const uint32_t need = sizeof(uint16_t) + sizeof(sys_device_remote_t);
  ERR_CHECK(in.len == need, ERR_CMD_BAD_LEN, .got = (uint16_t)in.len, .need = (uint16_t)need);
  uint16_t type_id;
  sys_device_remote_t where;
  memcpy(&type_id, in.data, sizeof(type_id));  // little-endian target
  memcpy(&where, in.data + sizeof(type_id), sizeof(where));
  ERR_CHECK(where.remote_id < SYS_DEVICE_COUNT &&
                (where.flags == SYS_DEVICE_REMOTE_WIRED || where.flags == SYS_DEVICE_REMOTE_WIRELESS),
            ERR_DEV_BAD_ARG);
  const sys_device_type_t* type = sys_device_type_find(type_id);
  ERR_CHECK(type != NULL, ERR_DEV_TYPE_UNKNOWN, .type = type_id);
  ERR_CHECK(!sys_device_slot_is_installed(id), ERR_DEV_ID_TAKEN, .id = id);
  const uint8_t twin = find_remote(&where);
  ERR_CHECK(twin == SYS_DEVICE_ID_NONE, ERR_DEV_ID_TAKEN, .id = twin);
  ERR_TRY(sys_device_slot_begin_create(id));

  sys_device_remote_t* instance = malloc(sizeof(*instance));
  ERR_CHECK(instance != NULL, ERR_DEV_NO_MEM, .id = id, .size = sizeof(*instance));
  *instance = where;
  const err_h err = sys_device_slot_install(id, type, instance, false, true);
  if (err != NULL) {
    free(instance);
  }
  return err;
}

err_h sys_device_remote_send(uint8_t id, const sys_device_remote_t* where, uint8_t dir, uint16_t op, cmd_in_t in,
                             cmd_out_t* out) {
  ERR_CHECK(s_remote != NULL, ERR_DEV_REMOTE_DOWN, .id = id, .board = where->board);
  return s_remote(where, dir, op, in, out);
}
