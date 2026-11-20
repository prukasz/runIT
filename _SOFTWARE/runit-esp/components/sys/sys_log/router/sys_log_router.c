#include "sys_log_router.h"

#include "sys_log.h"

ROUTER_READ(LOG, OP_CONFIG) {
  sys_log_cfg_t cfg;
  sys_log_get(&cfg);
  return cmd_reply(out, &cfg, sizeof(cfg));
}

ROUTER_SETTER(LOG, OP_CONFIG, sys_log_set)

ROUTER_READ(LOG, OP_ERRORS) {
  uint8_t reply[1 + ERR_RECORD_MAX];
  uint32_t total = 0;
  const uint32_t len = sys_log_error_get(args->index, reply + 1, &total);
  // An index past the end is not an error (history may be empty or shorter than asked): just total, no record.
  reply[0] = (uint8_t)total;
  return cmd_reply(out, reply, 1u + len);
}

static const router_op_t k_ops[] = {
    ROUTER_ENTRY(LOG, OP_CONFIG),
    ROUTER_ENTRY_R(LOG, OP_ERRORS),
};

ROUTER_MODULE(sys_log_router, k_ops)
ROUTER_MODULE_REGISTER(MODULE_ID_LOG, sys_log_router)
