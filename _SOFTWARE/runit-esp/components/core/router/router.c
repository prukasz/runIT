#include "router.h"

ERR_MODULE_DEFINE(ERR_MOD_ROUTER, "router", ERR_ROUTER_MAP)

// Filled at boot (router_register), read-only after router_init.
static struct {
  router_entry_t root[CONFIG_ROUTER_MAX_KEYS];
  uint32_t root_count;
} s_router;

err_h router_call(const router_entry_t* table, uint32_t count, void* ctx, const uint8_t* in, uint32_t in_len,
                  uint8_t* out, uint32_t out_cap, uint32_t* out_len) {
  ERR_CHECK(in_len > 0, ERR_ROUTER_NO_KEY);
  uint8_t key = in[0];
  for (uint32_t i = 0; i < count; i++) {
    if (table[i].key == key) {
      err_h err = table[i].fn(ctx, in, in_len, out, out_cap, out_len);
      return err != NULL ? ERR_WRAP(err, ERR_ROUTER_KEY, .key = key) : NULL;
    }
  }
  return ERR_NEW(ERR_ROUTER_UNKNOWN_KEY, .key = key);
}

err_h router_call_op(const router_op_t* table, uint32_t count, void* ctx, const uint8_t* in, uint32_t in_len,
                     uint8_t* out, uint32_t out_cap, uint32_t* out_len) {
  ERR_CHECK(in_len >= 2, ERR_ROUTER_NO_KEY);  // [dir][code]
  const uint8_t dir = in[0];
  const uint8_t code = in[1];
  for (uint32_t i = 0; i < count; i++) {
    if (table[i].code == code) {
      cmd_f fn = NULL;
      err_h err = cmd_side(table[i].read, table[i].write, dir, &fn);
      if (err == NULL) err = fn(ctx, in + 1, in_len - 1, out, out_cap, out_len);
      return err != NULL ? ERR_WRAP(err, ERR_ROUTER_KEY, .key = code) : NULL;
    }
  }
  return ERR_NEW(ERR_ROUTER_UNKNOWN_KEY, .key = code);
}

err_h router_args(const uint8_t* in, uint32_t in_len, void* args, uint32_t size) {
  uint32_t got = in_len > 0 ? in_len - 1 : 0;
  ERR_CHECK(got == size, ERR_CMD_BAD_LEN, .got = (uint16_t)got, .need = (uint16_t)size);
  if (size > 0) {
    memcpy(args, in + 1, size);
  }
  return NULL;
}

err_h router_args_tail(const uint8_t* in, uint32_t in_len, void* hdr, uint32_t size, const uint8_t** tail,
                       uint32_t* tail_len) {
  uint32_t got = in_len > 0 ? in_len - 1 : 0;
  ERR_CHECK(got > size, ERR_CMD_BAD_LEN, .got = (uint16_t)got, .need = (uint16_t)(size + 1));
  if (size > 0) {
    memcpy(hdr, in + 1, size);
  }
  *tail = in + 1 + size;
  *tail_len = got - size;
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

err_h router_route(const uint8_t* in, uint32_t in_len, uint8_t* out, uint32_t out_cap, uint32_t* out_len) {
  *out_len = 0;
  err_h err = router_call(s_router.root, s_router.root_count, NULL, in, in_len, out, out_cap, out_len);
  if (err != NULL) {
    *out_len = 0;
  }
  return err;
}
