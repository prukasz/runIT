#include "sys_device_policy_router.h"

#include "sys_device.h"

ROUTER_READ(POLICY, OP_POLICY) {
  sys_device_error_policy_t p;
  ERR_CHECK(sys_device_policy_get(args->id, &p), ERR_DEV_NOT_FOUND, .id = args->id);
  return CMD_REPLY(POLICY, OP_POLICY, .id = args->id, .policy = p);
}

ROUTER_WRITE(POLICY, OP_POLICY) {
  const sys_device_error_policy_t p = args->policy;  // out of the packed frame
  return sys_device_policy_set(args->id, &p);
}

static const router_op_t k_ops[] = {
    ROUTER_ENTRY(POLICY, OP_POLICY),
};

ROUTER_MODULE(sys_device_policy_router, k_ops)

MODULE_ID_DEFINE(POLICY, 0x21)
ROUTER_MODULE_REGISTER(POLICY, sys_device_policy_router)
