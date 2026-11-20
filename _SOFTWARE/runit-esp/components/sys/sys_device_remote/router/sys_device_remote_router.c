#include "sys_device_remote_router.h"

#include "router_queue.h"

// The subscriber is who sent the command: no peer names another.
ROUTER_WRITE(REMOTE, OP_SUBSCRIBE) {
  const cmd_req_t* origin = router_origin();
  ERR_CHECK(origin != NULL, ERR_CMD_BAD_ARG);
  return sys_device_remote_subscribe(args->id, origin->src);
}

ROUTER_WRITE(REMOTE, OP_UNSUBSCRIBE) {
  const cmd_req_t* origin = router_origin();
  ERR_CHECK(origin != NULL, ERR_CMD_BAD_ARG);
  sys_device_remote_unsubscribe(args->id, origin->src);
  return NULL;
}

static const router_op_t k_ops[] = {
    ROUTER_ENTRY_W(REMOTE, OP_SUBSCRIBE),
    ROUTER_ENTRY_W(REMOTE, OP_UNSUBSCRIBE),
};

ROUTER_MODULE(sys_device_remote_router, k_ops)
ROUTER_MODULE_REGISTER(MODULE_ID_REMOTE, sys_device_remote_router)
