#include "sys_log_router.h"
#include "sys_log.h"

// The options, applied at once (not stored).
ROUTER_OP(op_set, sys_log_cfg_t) {
  return sys_log_set(args);
}

ROUTER_OP_NOARGS(op_get) {
  sys_log_cfg_t cfg;
  sys_log_get(&cfg);
  return cmd_reply(out, out_cap, out_len, &cfg, sizeof(cfg));
}

// One kept error record: [total u8][record], only [total] for an index past the end. The app asks index 0 up to total - 1 (the oldest first).
typedef struct __attribute__((packed)) {
  uint8_t index;
} sys_log_errors_args_t;

ROUTER_OP(op_errors, sys_log_errors_args_t) {
  uint8_t reply[1 + SYS_LOG_RECORD_MAX];
  uint32_t total = 0;
  const uint32_t len = sys_log_error_get(args->index, reply + 1, &total);
  // An index past the end is not an error (the history may be empty, or shorter than asked): just the count, no record.
  reply[0] = (uint8_t)total;
  return cmd_reply(out, out_cap, out_len, reply, 1 + len);
}

static const router_op_t k_ops[] = {
    ROUTER_RW(SYS_LOG_OP_CONFIG, op_get, op_set),
    ROUTER_R(SYS_LOG_OP_ERRORS, op_errors),
};

ROUTER_MODULE(sys_log_router, k_ops)
