#include "sys_log_router.h"
#include "sys_log.h"

// The options and where they go: stored, applied at once.
ROUTER_OP(op_set, sys_log_cfg_t) {
  ERR_CHECK(args->mirror_serial <= 1 && args->trace_errors <= 1 && args->send_errors <= 1 && args->send_logs <= 1, ERR_CMD_BAD_ARG);
  return sys_log_set(args);
}

ROUTER_OP_NOARGS(op_get) {
  sys_log_cfg_t cfg;
  sys_log_get(&cfg);
  return cmd_reply(out, out_cap, out_len, &cfg, sizeof(cfg));
}

static const router_entry_t k_ops[] = {
    {SYS_LOG_OP_SET, op_set},
    {SYS_LOG_OP_GET, op_get},
};

ROUTER_MODULE(sys_log_router, k_ops)
