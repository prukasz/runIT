#include "sys_remote_router.h"

// The subscriber is who sent the command: no peer names another.
ROUTER_OP(op_subscribe, sys_remote_device_ref_t) {
  const cmd_req_t* origin = router_origin();
  ERR_CHECK(origin != NULL, ERR_CMD_BAD_ARG);
  return sys_remote_subscribe(args->id, origin->src);
}

ROUTER_OP(op_unsubscribe, sys_remote_device_ref_t) {
  const cmd_req_t* origin = router_origin();
  ERR_CHECK(origin != NULL, ERR_CMD_BAD_ARG);
  sys_remote_unsubscribe(args->id, origin->src);
  return NULL;
}

static const router_op_t k_ops[] = {
    ROUTER_W(SYS_REMOTE_OP_SUBSCRIBE, op_subscribe),
    ROUTER_W(SYS_REMOTE_OP_UNSUBSCRIBE, op_unsubscribe),
};

ROUTER_MODULE(sys_remote_router, k_ops)
