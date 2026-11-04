#include "sys_stream_router.h"

#include "sdkconfig.h"
#include "sys_stream_internal.h"

typedef struct __attribute__((packed)) {
  uint8_t id;  /**< The stream */
} stream_ref_t;

typedef struct __attribute__((packed)) {
  uint8_t id;                       /**< The stream */
  sys_data_provider_id_t provider;  /**< The provider */
} binding_t;

ROUTER_OP(op_create, sys_stream_cfg_t) {
  return sys_stream_create(args);
}

ROUTER_OP(op_remove, stream_ref_t) {
  return sys_stream_remove(args->id);
}

ROUTER_OP(op_bind, binding_t) {
  return sys_stream_bind(args->id, args->provider);
}

ROUTER_OP(op_unbind, binding_t) {
  return sys_stream_unbind(args->id, args->provider);
}

ROUTER_OP_NOARGS(op_list) {
  sys_stream_info_t list[CONFIG_SYS_STREAM_MAX];
  const uint8_t count = sys_stream_list(list);
  ERR_TRY(cmd_append(out, &count, sizeof(count)));
  for (uint8_t n = 0; n < count; n++) {
    const sys_stream_info_t* i = &list[n];
    ERR_TRY(cmd_append(out, &i->head, sizeof(i->head)));
    ERR_TRY(cmd_append(out, i->targets, i->head.targets * sizeof(i->targets[0])));
  }
  return NULL;
}

// The subscriber is who sent the command: no peer names another.
ROUTER_OP(op_subscribe, stream_ref_t) {
  const cmd_req_t* origin = router_origin();
  ERR_CHECK(origin != NULL, ERR_STREAM_BAD_ARG);
  return sys_stream_subscribe(args->id, origin->src);
}

ROUTER_OP(op_unsubscribe, stream_ref_t) {
  const cmd_req_t* origin = router_origin();
  ERR_CHECK(origin != NULL, ERR_STREAM_BAD_ARG);
  return sys_stream_unsubscribe(args->id, origin->src);
}

static const router_op_t k_ops[] = {
    ROUTER_W(SYS_STREAM_OP_CREATE, op_create),
    ROUTER_W(SYS_STREAM_OP_REMOVE, op_remove),
    ROUTER_W(SYS_STREAM_OP_BIND, op_bind),
    ROUTER_W(SYS_STREAM_OP_UNBIND, op_unbind),
    ROUTER_R(SYS_STREAM_OP_LIST, op_list),
    ROUTER_W(SYS_STREAM_OP_SUBSCRIBE, op_subscribe),
    ROUTER_W(SYS_STREAM_OP_UNSUBSCRIBE, op_unsubscribe),
};

ROUTER_MODULE(sys_stream_router, k_ops)
