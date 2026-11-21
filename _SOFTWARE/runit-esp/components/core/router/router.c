#include "router.h"

ERR_MODULE_DEFINE(MODULE_ID_ROUTER, ERR_ROUTER_MAP)

// Filled at boot (router_register), read-only after router_init.
static struct {
  router_entry_t root[CONFIG_ROUTER_MAX_KEYS];
  uint32_t root_count;
} s_router;

err_h router_call(const router_entry_t* table, uint32_t count, void* ctx, cmd_in_t in, cmd_out_t* out) {
  ERR_CHECK(in.len > 0, ERR_ROUTER_NO_KEY);
  uint8_t key = in.data[0];
  for (uint32_t i = 0; i < count; i++) {
    if (table[i].key == key) {
      err_h err = table[i].fn(ctx, in, out);
      return err != NULL ? ERR_WRAP(err, ERR_ROUTER_KEY, .key = key) : NULL;
    }
  }
  return ERR_NEW(ERR_ROUTER_UNKNOWN_KEY, .key = key);
}

err_h router_call_op(const router_op_t* table, uint32_t count, void* ctx, cmd_in_t in, cmd_out_t* out) {
  ERR_CHECK(in.len >= 2, ERR_ROUTER_NO_KEY);  // [dir][code]
  const uint8_t dir = in.data[0];
  const uint8_t code = in.data[1];
  for (uint32_t i = 0; i < count; i++) {
    if (table[i].code == code) {
      const cmd_f fn = dir == CMD_DIR_READ ? table[i].read : dir == CMD_DIR_WRITE ? table[i].write : NULL;
      err_h err = fn != NULL ? fn(ctx, (cmd_in_t){in.data + 2, in.len - 2}, out)
                             : ERR_NEW(ERR_CMD_DIRECTION, .dir = dir);
      return err != NULL ? ERR_WRAP(err, ERR_ROUTER_OP, .code = code) : NULL;
    }
  }
  return ERR_NEW(ERR_ROUTER_UNKNOWN_OP, .code = code);
}

static err_h root_register(uint8_t key, cmd_f fn) {
  ERR_CHECK(key != 0 && fn != NULL, ERR_BAD_ARG);
  for (uint32_t i = 0; i < s_router.root_count; i++) {
    ERR_CHECK(s_router.root[i].key != key, ERR_ROUTER_KEY_TAKEN, .key = key);
  }
  ERR_CHECK(s_router.root_count < CONFIG_ROUTER_MAX_KEYS, ERR_ROUTER_TABLE_FULL, .key = key);
  s_router.root[s_router.root_count++] = (router_entry_t){.key = key, .fn = fn};
  return NULL;
}

err_h router_route(cmd_in_t in, cmd_out_t* out) {
  out->len = 0;
  err_h err = router_call(s_router.root, s_router.root_count, NULL, in, out);
  if (err != NULL) {
    out->len = 0;
  }
  return err;
}

/* The module routers kept by their constructors (ROUTER_MODULE_REGISTER), bound into the root table by
 * router_root_init: a constructor runs before the errors are ready, so it only writes the pair down. One past
 * the table is kept as `dropped`. */
static struct {
  router_entry_t entry[CONFIG_ROUTER_MAX_KEYS];
  uint32_t count;
  uint8_t dropped;  // the key of the first router past the table, 0 when none
} s_later;

void router_register(uint8_t key, cmd_f fn) {
  if (s_later.count < CONFIG_ROUTER_MAX_KEYS) {
    s_later.entry[s_later.count++] = (router_entry_t){.key = key, .fn = fn};
  } else if (s_later.dropped == 0) {
    s_later.dropped = key;
  }
}

err_h router_root_init(void) {
  const uint8_t dropped = s_later.dropped;
  const uint32_t count = s_later.count;
  s_later.dropped = 0;
  s_later.count = 0;
  ERR_CHECK(dropped == 0, ERR_ROUTER_TABLE_FULL, .key = dropped);
  for (uint32_t i = 0; i < count; i++) {
    ERR_TRY(root_register(s_later.entry[i].key, s_later.entry[i].fn));
  }
  return NULL;
}
