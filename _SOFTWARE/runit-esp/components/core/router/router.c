#include "router.h"

ERR_MODULE_DEFINE(ERR_MOD_ROUTER, "router", ERR_ROUTER_MAP)

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
      cmd_f fn = NULL;
      err_h err = cmd_side(table[i].read, table[i].write, dir, &fn);
      if (err == NULL) err = fn(ctx, (cmd_in_t){in.data + 1, in.len - 1}, out);
      return err != NULL ? ERR_WRAP(err, ERR_ROUTER_KEY, .key = code) : NULL;
    }
  }
  return ERR_NEW(ERR_ROUTER_UNKNOWN_KEY, .key = code);
}

err_h router_args(cmd_in_t in, void* args, uint32_t size) {
  uint32_t got = in.len > 0 ? in.len - 1 : 0;
  ERR_CHECK(got == size, ERR_CMD_BAD_LEN, .got = (uint16_t)got, .need = (uint16_t)size);
  if (size > 0) {
    memcpy(args, in.data + 1, size);
  }
  return NULL;
}

err_h router_args_tail(cmd_in_t in, void* hdr, uint32_t size, cmd_in_t* tail) {
  uint32_t got = in.len > 0 ? in.len - 1 : 0;
  ERR_CHECK(got > size, ERR_CMD_BAD_LEN, .got = (uint16_t)got, .need = (uint16_t)(size + 1));
  if (size > 0) {
    memcpy(hdr, in.data + 1, size);
  }
  *tail = (cmd_in_t){in.data + 1 + size, got - size};
  return NULL;
}

err_h router_register(uint8_t key, cmd_f fn) {
  ERR_CHECK(key != 0 && fn != NULL, ERR_CMD_BAD_ARG);
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
